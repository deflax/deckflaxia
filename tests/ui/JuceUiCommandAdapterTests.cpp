#include "ui/JuceUiCommandAdapter.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int expectStatus(const deckflaxia::ui::JuceUiCommandResult& result,
                 deckflaxia::ui::JuceUiCommandStatus status,
                 const std::string& message) {
    if (result.status != status) {
        std::cerr << "FAILED: " << message << " expected=" << deckflaxia::ui::toString(status)
                  << " actual=" << deckflaxia::ui::toString(result.status) << " detail=" << result.detail << '\n';
        return 1;
    }
    return 0;
}

int testDeckTransport() {
    deckflaxia::decks::FourDeckPlaybackCore core;
    deckflaxia::ui::JuceUiCommandAdapter adapter({&core});
    const auto unloaded = adapter.dispatch(deckflaxia::ui::DeckTransportIntent{deckflaxia::ui::DeckTransportAction::Play, 0});
    if (expectStatus(unloaded, deckflaxia::ui::JuceUiCommandStatus::Unavailable, "unloaded deck play should not fake success") != 0) {
        return 1;
    }
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    const auto media = deckflaxia::decks::PreparedAudioMedia::deterministicTestWav(2048, 48000);
    if (expect(core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::deterministicTestWav(media)).ok(), "test media should load before transport") != 0) {
        return 1;
    }
    const auto play = adapter.dispatch(deckflaxia::ui::DeckTransportIntent{deckflaxia::ui::DeckTransportAction::Play, 0});
    if (expectStatus(play, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "deck play should dispatch") != 0) {
        return 1;
    }
    if (expect(play.action == "play" && play.detail == "deck-command-applied", "deck play should report exact adapter command result") != 0) {
        return 1;
    }
    if (expect(core.deck(deckId).state().transport.playing, "deck play should reach playback core") != 0) {
        return 1;
    }
    const auto invalid = adapter.dispatch(deckflaxia::ui::DeckTransportIntent{deckflaxia::ui::DeckTransportAction::Cue, 9});
    return expectStatus(invalid, deckflaxia::ui::JuceUiCommandStatus::InvalidArgument, "invalid deck transport should be explicit");
}

int testMixer() {
    deckflaxia::audio::MixerController mixer;
    deckflaxia::audio::routing::AudioRoutingGraphController routing(deckflaxia::audio::routing::RoutingDeviceLayout::forChannelCount(8));
    deckflaxia::ui::JuceUiCommandAdapter adapter({nullptr, &mixer, &routing});
    const auto volume = adapter.dispatch(deckflaxia::ui::MixerIntent{deckflaxia::ui::MixerAction::Volume, 1, 0.25F});
    if (expectStatus(volume, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "mixer volume should dispatch") != 0) {
        return 1;
    }
    if (expect(volume.action == "volume" && volume.detail == "mixer-command-applied", "mixer volume should report exact adapter command result") != 0) {
        return 1;
    }
    if (expect(std::abs(mixer.activeSnapshot().decks[1].volume - 0.25F) < 0.000001F, "mixer volume should update authoritative snapshot") != 0) {
        return 1;
    }
    const auto invalid = adapter.dispatch(deckflaxia::ui::MixerIntent{deckflaxia::ui::MixerAction::Gain, 99, 0.5F});
    return expectStatus(invalid, deckflaxia::ui::JuceUiCommandStatus::InvalidArgument, "invalid mixer deck should be explicit");
}

int testBrowser() {
    std::vector<deckflaxia::library::AudioImportClassification> rows;
    deckflaxia::ui::JuceUiCommandAdapter adapter({nullptr, nullptr, nullptr, nullptr, nullptr, &rows});
    const auto imported = adapter.dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::Import,
                                                                          deckflaxia::library::FilesystemEntry{"tests/fixtures/dj-workflow/track_120bpm.wav", true}});
    if (expectStatus(imported, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "browser import should classify supported audio") != 0) {
        return 1;
    }
    if (expect(rows.size() == 1U && rows[0].importable(), "browser import should append authoritative classification") != 0) {
        return 1;
    }
    const auto load = adapter.dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::LoadToDeck,
                                                                      deckflaxia::library::FilesystemEntry{"tests/fixtures/dj-workflow/track_120bpm.wav", true},
                                                                      0});
    if (expectStatus(load, deckflaxia::ui::JuceUiCommandStatus::Unsupported, "browser load without runtime backend should be unsupported") != 0) {
        return 1;
    }
    const auto rejected = adapter.dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::Import,
                                                                         deckflaxia::library::FilesystemEntry{"tests/fixtures/dj-workflow/not_audio.txt", true}});
    if (expectStatus(rejected, deckflaxia::ui::JuceUiCommandStatus::Unavailable, "unsupported browser import should be explicit") != 0) {
        return 1;
    }

    deckflaxia::decks::FourDeckPlaybackCore core;
    deckflaxia::ui::JuceUiCommandAdapter loadAdapter({&core, nullptr, nullptr, nullptr, nullptr, &rows});
    const auto loaded = loadAdapter.dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::LoadToDeck,
                                                                           deckflaxia::library::FilesystemEntry{"tests/fixtures/dj-workflow/track_120bpm.wav", true},
                                                                           2});
    if (expectStatus(loaded, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "browser load with playback core should dispatch") != 0) {
        return 1;
    }
    const auto deckOne = deckflaxia::core::DeckId::fromIndex(0).value;
    const auto deckThree = deckflaxia::core::DeckId::fromIndex(2).value;
    return expect(!core.deck(deckOne).state().loaded && core.deck(deckThree).state().loaded,
                  "browser load should target requested deck, not a stale/global deck");
}

int testPluginChain() {
    auto first = deckflaxia::plugins::makeDeterministicGainPlugin(0.5, false);
    auto second = deckflaxia::plugins::makeDeterministicGainPlugin(0.25, false);
    second.identifier = "deterministic:gain-b";
    deckflaxia::core::PluginChainDescriptor descriptor{"deck-a", {first, second}};
    deckflaxia::ui::JuceUiCommandAdapter descriptorAdapter({nullptr, nullptr, nullptr, nullptr, &descriptor});
    const auto bypass = descriptorAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Bypass,
                                                                                     deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                     0,
                                                                                     0,
                                                                                     true});
    if (expectStatus(bypass, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "plugin descriptor bypass should dispatch") != 0) {
        return 1;
    }
    if (expect(descriptor.plugins[0].bypassed, "plugin descriptor bypass should mutate authoritative descriptor") != 0) {
        return 1;
    }
    const auto descriptorParameter = descriptorAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Parameter,
                                                                                                 deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                                 0,
                                                                                                 0,
                                                                                                 false,
                                                                                                 "gain",
                                                                                                 0.375});
    if (expectStatus(descriptorParameter, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "plugin descriptor parameter should dispatch") != 0) {
        return 1;
    }
    if (expect(std::abs(descriptor.plugins[0].parameters[0].normalizedValue - 0.375) < 0.000001, "plugin descriptor parameter should mutate authoritative descriptor") != 0) {
        return 1;
    }
    const auto moved = descriptorAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::MoveDown,
                                                                                    deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                    0,
                                                                                    0});
    if (expectStatus(moved, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "plugin descriptor move should dispatch") != 0) {
        return 1;
    }
    if (expect(descriptor.plugins[1].identifier == first.identifier, "plugin descriptor move should reach VST3EditorUi helper") != 0) {
        return 1;
    }
    const auto removed = descriptorAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Remove,
                                                                                     deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                     0,
                                                                                     0});
    if (expectStatus(removed, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "plugin descriptor remove should dispatch") != 0) {
        return 1;
    }
    if (expect(descriptor.plugins.size() == 1U && descriptor.plugins[0].identifier == first.identifier, "plugin descriptor remove should mutate authoritative descriptor") != 0) {
        return 1;
    }
    deckflaxia::core::PluginChainDescriptor emptyDescriptor{"empty", {}};
    deckflaxia::ui::JuceUiCommandAdapter emptyAdapter({nullptr, nullptr, nullptr, nullptr, &emptyDescriptor});
    const auto emptyBypass = emptyAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Bypass,
                                                                                    deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                    0,
                                                                                    0,
                                                                                    true});
    if (expectStatus(emptyBypass, deckflaxia::ui::JuceUiCommandStatus::InvalidArgument, "empty descriptor bypass should not report success") != 0) {
        return 1;
    }

    deckflaxia::plugins::OfflinePluginChainHost host;
    if (expect(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, descriptor, 48000.0, 512).ok(), "plugin host should configure") != 0) {
        return 1;
    }
    deckflaxia::ui::JuceUiCommandAdapter hostAdapter({nullptr, nullptr, nullptr, &host});
    const auto parameter = hostAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Parameter,
                                                                                  deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                  0,
                                                                                  0,
                                                                                  false,
                                                                                  "gain",
                                                                                  0.125});
    if (expectStatus(parameter, deckflaxia::ui::JuceUiCommandStatus::Succeeded, "plugin parameter should dispatch") != 0) {
        return 1;
    }
    if (expect(std::abs(host.parameter(0, "gain") - 0.125) < 0.000001, "plugin parameter should reach host") != 0) {
        return 1;
    }
    const auto remove = hostAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Remove,
                                                                               deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                               0,
                                                                               0});
    if (expectStatus(remove, deckflaxia::ui::JuceUiCommandStatus::Unavailable, "runtime-only remove should not report success") != 0) {
        return 1;
    }
    const auto invalid = hostAdapter.dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Parameter,
                                                                                deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                0,
                                                                                99,
                                                                                false,
                                                                                "gain",
                                                                                0.5});
    return expectStatus(invalid, deckflaxia::ui::JuceUiCommandStatus::InvalidArgument, "invalid plugin parameter slot should be explicit");
}

}

int main() {
    if (testDeckTransport() != 0 || testMixer() != 0 || testBrowser() != 0 || testPluginChain() != 0) {
        return 1;
    }
    std::cout << "Juce UI command adapter tests passed\n";
    return 0;
}
