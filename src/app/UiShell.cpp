#include "app/UiShell.h"

#include <iostream>
#include <sstream>
#include <utility>

namespace djapp::app {

namespace {

std::string deckLabel(std::size_t deckIndex) {
    return "deck-panel-" + std::to_string(deckIndex + 1U);
}

std::string deckDisplayName(std::size_t deckIndex) {
    static const std::array<const char*, audio::routing::kDeckCount> names{{"Deck A", "Deck B", "Deck C", "Deck D"}};
    return names[deckIndex];
}

std::string deckAccentName(std::size_t deckIndex) {
    static const std::array<const char*, audio::routing::kDeckCount> accents{{"signal amber", "cue cyan", "meter lime", "record red"}};
    return accents[deckIndex];
}

std::string pluginSlotStatus(audio::routing::PluginSlotState state) {
    switch (state) {
    case audio::routing::PluginSlotState::EmptyPlaceholder:
        return "empty plugin placeholder";
    case audio::routing::PluginSlotState::HostedPluginPlaceholder:
        return "hosted plugin placeholder";
    case audio::routing::PluginSlotState::MissingPluginPlaceholder:
        return "missing plugin recoverable placeholder";
    }
    return "unknown plugin placeholder";
}

BrowserTrackViewModel toBrowserTrack(const library::BrowserTrackEntry& entry) {
    return BrowserTrackViewModel{entry.track.id,
                                 entry.track.title,
                                 entry.track.artist,
                                 entry.availability == library::TrackAvailability::Missing,
                                 entry.waveform.summaryPointCount > 0U};
}

std::vector<float> waveformPointsForDeck(const std::vector<library::BrowserTrackEntry>& tracks, std::size_t deckIndex) {
    if (deckIndex >= tracks.size() || tracks[deckIndex].waveform.summaryPointCount == 0U) {
        return {};
    }
    return std::vector<float>(tracks[deckIndex].waveform.summaryPointCount, 0.5F);
}

} // namespace

HybridUiShellModel::HybridUiShellModel(rendering::WaveformRenderer renderer) : renderer_(std::move(renderer)) {}

HybridUiShellSnapshot HybridUiShellModel::buildSnapshot(const HybridUiShellInputSnapshot& input) const {
    HybridUiShellSnapshot snapshot;

    snapshot.browser.empty = input.browserTracks.empty();
    snapshot.browser.statusText = snapshot.browser.empty ? "empty-browser: drop tracks to ignite the library" : "browser-ready: tracks=" + std::to_string(input.browserTracks.size());
    snapshot.browser.tracks.reserve(input.browserTracks.size());
    for (const auto& track : input.browserTracks) {
        snapshot.browser.tracks.push_back(toBrowserTrack(track));
    }

    for (std::size_t deckIndex = 0; deckIndex < snapshot.decks.size(); ++deckIndex) {
        auto& deck = snapshot.decks[deckIndex];
        deck.componentName = deckLabel(deckIndex);
        deck.displayName = deckDisplayName(deckIndex);
        deck.accentName = deckAccentName(deckIndex);
        deck.waveform = renderer_.renderWaveform(rendering::WaveformRenderRequest{"deck:" + std::to_string(deckIndex + 1U), waveformPointsForDeck(input.browserTracks, deckIndex), 640, 160});
        deck.meter = renderer_.renderMeter(rendering::MeterRenderRequest{0.0F, 0.0F, 120});
    }

    snapshot.routing.nodeCount = input.routing.nodeCount;
    snapshot.routing.connectionCount = input.routing.connectionCount;
    snapshot.routing.statusText = input.routing.warning == audio::routing::RoutingGraphWarning::CueMasterOverlap
                                      ? "routing-warning: cue shares master output"
                                      : "routing-ready: deck graph snapshot";

    snapshot.pluginChain.slots.reserve(audio::routing::kDeckCount * audio::routing::kPluginSlotsPerDeck);
    for (const auto& deck : input.routing.decks) {
        for (const auto& slot : deck.pluginSlots) {
            snapshot.pluginChain.slots.push_back(PluginSlotViewModel{"plugin-slot-deck-" + std::to_string(deck.deckId.index() + 1U) + "-" + std::to_string(slot.slotIndex + 1U),
                                                                    pluginSlotStatus(slot.state),
                                                                    slot.placeholder});
        }
    }

    snapshot.midiLearn.learning = input.midiLearn.learning;
    snapshot.midiLearn.mappingCount = input.midiLearn.mappingCount;
    snapshot.midiLearn.statusText = input.midiLearn.learning
                                        ? "midi-learn-armed: " + input.midiLearn.learningTargetName
                                        : "midi-learn-idle: mappings=" + std::to_string(input.midiLearn.mappingCount);

    snapshot.status.ok = true;
    snapshot.status.statusText = "status-ok: industrial console shell initialized";
    return snapshot;
}

std::string HybridUiShellModel::formatSmokeReport(const HybridUiShellSnapshot& snapshot) const {
    std::ostringstream output;
    output << "ui-smoke-test: ok\n";
    output << "ui-shell: hybrid-juce-compatible industrial-console\n";
    output << "deck-panels: " << snapshot.decks.size() << '\n';
    for (const auto& deck : snapshot.decks) {
        output << "component: " << deck.componentName << " name=\"" << deck.displayName << "\" accent=\"" << deck.accentName << "\" waveform="
               << (deck.waveform.placeholder ? "placeholder" : "ready") << " meter=" << deck.meter.statusText << '\n';
    }
    output << "component: " << snapshot.browser.componentName << " tracks=" << snapshot.browser.tracks.size() << " empty=" << (snapshot.browser.empty ? "true" : "false")
           << " status=\"" << snapshot.browser.statusText << "\"\n";
    output << "component: " << snapshot.routing.componentName << " nodes=" << snapshot.routing.nodeCount << " connections=" << snapshot.routing.connectionCount << " status=\""
           << snapshot.routing.statusText << "\"\n";
    output << "component: " << snapshot.pluginChain.componentName << " slots=" << snapshot.pluginChain.slots.size() << " placeholders=true\n";
    output << "component: " << snapshot.midiLearn.componentName << " learning=" << (snapshot.midiLearn.learning ? "true" : "false") << " mappings=" << snapshot.midiLearn.mappingCount
           << " status=\"" << snapshot.midiLearn.statusText << "\"\n";
    output << "component: " << snapshot.status.componentName << " ok=" << (snapshot.status.ok ? "true" : "false") << " status=\"" << snapshot.status.statusText << "\"\n";
    output << "renderer: waveform backend=" << rendering::toString(snapshot.decks[0].waveform.backend) << " placeholder=" << (snapshot.decks[0].waveform.placeholder ? "true" : "false") << '\n';
    return output.str();
}

HybridUiShellInputSnapshot createUiSmokeInput(bool emptyLibrary) {
    HybridUiShellInputSnapshot input;
    input.routing = audio::routing::AudioRoutingGraphSnapshot::createDefault(audio::routing::RoutingDeviceLayout::forChannelCount(4));
    input.midiLearn = MidiLearnIndicatorSnapshot{false, midi::MidiLearnTargetRegistry::createAlphaDefault().size(), {}};

    if (!emptyLibrary) {
        library::BrowserTrackEntry first;
        first.track = core::LibraryTrack{"track:smoke-a", "North Pier Signal", "Bootstrap Crew", core::BeatgridMetadata::fromBpm(124.0, 0.0).value, core::MusicalKey::Camelot8A};
        first.path = "/alpha/North Pier Signal.wav";
        first.waveform = library::WaveformCacheMetadata{first.track.id, 8, 181.0};
        input.browserTracks.push_back(first);

        library::BrowserTrackEntry second;
        second.track = core::LibraryTrack{"track:smoke-b", "Basement Meter Drift", "Bootstrap Crew", core::BeatgridMetadata::fromBpm(128.0, 0.0).value, core::MusicalKey::Camelot8B};
        second.path = "/alpha/Basement Meter Drift.wav";
        input.browserTracks.push_back(second);
    }
    return input;
}

int runUiSmokeTest(bool emptyLibrary) {
    const HybridUiShellModel shell;
    const auto snapshot = shell.buildSnapshot(createUiSmokeInput(emptyLibrary));
    const auto report = shell.formatSmokeReport(snapshot);
    std::cout << report;

    if (snapshot.decks.size() != audio::routing::kDeckCount || snapshot.browser.componentName.empty() || snapshot.routing.componentName.empty() ||
        snapshot.pluginChain.componentName.empty() || snapshot.midiLearn.componentName.empty() || snapshot.status.componentName.empty()) {
        return 1;
    }
    if (emptyLibrary && (!snapshot.browser.empty || !snapshot.decks[0].waveform.placeholder)) {
        return 1;
    }
    return 0;
}

}
