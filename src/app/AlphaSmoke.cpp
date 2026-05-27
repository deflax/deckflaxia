#include "app/AlphaSmoke.h"

#include "analysis/AnalysisPipeline.h"
#include "app/UiShell.h"
#include "audio/AudioEngine.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"
#include "decks/AudioDeck.h"
#include "decks/SequencerDeck.h"
#include "library/LibraryModel.h"
#include "midi/MidiLearn.h"
#include "persistence/Persistence.h"
#include "plugins/PluginManager.h"

#include <ostream>
#include <string>

namespace djapp::app {

namespace {

bool require(bool condition, const std::string& label, std::ostream& output) {
    if (!condition) {
        output << "alpha-smoke-check: fail " << label << '\n';
        return false;
    }
    output << "alpha-smoke-check: ok " << label << '\n';
    return true;
}

core::BackgroundJobTicket databaseTicket(std::uint64_t id) noexcept {
    return core::BackgroundJobTicket{id, core::BackgroundJobKind::PersistLibraryChange, core::BackgroundWorkerRole::DatabaseWorker};
}

core::BackgroundJobTicket analysisTicket(std::uint64_t id) noexcept {
    return core::BackgroundJobTicket{id, core::BackgroundJobKind::AnalyzeTrack, core::BackgroundWorkerRole::AnalysisPool};
}

core::BackgroundJobTicket pluginScanTicket(std::uint64_t id) noexcept {
    return core::BackgroundJobTicket{id, core::BackgroundJobKind::ScanPlugins, core::BackgroundWorkerRole::PluginScanWorker};
}

library::ProLibraryRepository makeLibrary(persistence::PersistenceService& service) {
    return library::ProLibraryRepository{service.libraryTracks(), service.crates(), service.playlists(), service.trackMetadata()};
}

core::MasterClockState runningClock(double bpm, double positionBeats) {
    auto clock = core::MasterClockState::stopped(bpm);
    clock.positionBeats = positionBeats;
    clock.start();
    return clock;
}

core::MidiStepPattern deterministicSequencerPattern() {
    auto pattern = core::MidiStepPattern::sixteenStepDefault(60);
    pattern.steps[0] = core::MidiStep{true, 60, 100, 0.25};
    pattern.steps[4] = core::MidiStep{true, 67, 96, 0.25};
    pattern.steps[8] = core::MidiStep{true, 72, 92, 0.25};
    pattern.steps[12] = core::MidiStep{true, 76, 88, 0.25};
    return pattern;
}

} // namespace

int runAlphaSmokeTest(std::ostream& output) {
    using namespace audio;
    using namespace audio::routing;
    using namespace core;
    using namespace decks;
    using namespace library;
    using namespace midi;
    using namespace persistence;
    using namespace plugins;

    output << "alpha-smoke-test: start\n";

    PersistenceService persistence;
    if (!require(persistence.migrateOnDatabaseWorker(databaseTicket(1)).ok(), "persistence migration on database worker", output)) {
        return 1;
    }
    if (!require(persistence.appPreferences().put(AppPreference{"alpha.audio.backend", toString(currentBackendPolicy().preferred)}).ok(), "preference save", output)) {
        return 1;
    }

    auto decks = FourDeckCollection::createDefault();
    const auto deck1 = DeckId::fromIndex(0).value;
    const auto deck2 = DeckId::fromIndex(1).value;
    const auto deck3 = DeckId::fromIndex(2).value;
    if (!require(decks.size() == 4, "exactly four core deck slots", output)) {
        return 1;
    }
    if (!require(decks.setDeckType(deck1, DeckType::AudioFile).ok() && decks.setDeckType(deck2, DeckType::MidiStepSequencer).ok(), "audio and MIDI deck types", output)) {
        return 1;
    }

    ProLibraryRepository library = makeLibrary(persistence);
    const auto emptyLibrary = library.browserTracks();
    if (!require(emptyLibrary.ok() && emptyLibrary.value.empty(), "empty library state", output)) {
        return 1;
    }

    LibraryScanWorkerModel libraryWorker;
    const FolderImportRequest importRequest{14,
                                            "crate-alpha-smoke",
                                            "Alpha Smoke Crate",
                                            {FilesystemEntry{"/alpha-media/smoke-a.wav", true},
                                             FilesystemEntry{"/alpha-media/ignored.txt", true},
                                             FilesystemEntry{"/alpha-media/smoke-b.flac", true}}};
    const auto importResult = library.importFolderOnBackgroundWorker(importRequest, databaseTicket(2), libraryWorker);
    if (!require(importResult.ok() && importResult.importedTracks.size() == 2 && importResult.skippedEntries == 1, "test media import", output)) {
        return 1;
    }

    analysis::StubTrackAnalyzer analyzer;
    analysis::AnalysisWorkerModel analysisWorker;
    analysis::AnalysisJobQueue analysisQueue(persistence.analysisJobs(), library, analyzer);
    const auto analysisResult = analysisQueue.enqueueAndRun(importResult.importedTracks[0], analysisTicket(3), analysisWorker);
    if (!require(analysisResult.ok() && analysisResult.job.status == AnalysisJobStatus::Complete && analysisResult.waveform.summaryPointCount > 0U, "analysis metadata persistence", output)) {
        return 1;
    }
    if (!require(library.markTrackMissingByPath("/alpha-media/smoke-b.flac").ok(), "deleted track marked missing", output)) {
        return 1;
    }
    const auto browserTracks = library.browserTracks();
    if (!require(browserTracks.ok() && browserTracks.value.size() == 2 && browserTracks.value[1].availability == TrackAvailability::Missing, "browser shows imported and missing tracks", output)) {
        return 1;
    }

    ProLibraryRepository reloadedLibrary = makeLibrary(persistence);
    const auto reloadedTracks = reloadedLibrary.browserTracks();
    if (!require(reloadedTracks.ok() && reloadedTracks.value.size() == 2, "library reload from in-memory repository", output)) {
        return 1;
    }

    AudioRoutingGraphController graph(RoutingDeviceLayout::forChannelCount(8));
    if (!require(graph.enqueueAssignDeckOutput(deck1, OutputBus::Output2).ok() && graph.enqueueSetCueEnabled(deck1, true).ok(), "deck output and cue assignment queued", output)) {
        return 1;
    }
    if (!require(graph.processPendingUpdatesOutsideCallback().ok(), "routing graph update outside callback", output)) {
        return 1;
    }
    const auto routedSnapshot = graph.captureSnapshotForAudioCallback();
    if (!require(routedSnapshot.deck(deck1).assignment.mainOutput == OutputBus::Output2 && routedSnapshot.deck(deck1).assignment.cueEnabled, "audio deck routes output cue and master", output)) {
        return 1;
    }
    if (!require(persistence.routingConfig().save(RoutingConfigRecord{deck1.index(), routedSnapshot.deck(deck1).assignment}).ok(), "routing save", output)) {
        return 1;
    }

    AudioFileDeck audioDeck(deck1);
    const auto media = AudioDeckMediaReference::deterministicTestWav(PreparedAudioMedia::deterministicTestWav(512, 44100));
    if (!require(audioDeck.loadPreparedMedia(media).ok(), "audio deck load deterministic media", output)) {
        return 1;
    }
    audioDeck.play();
    const auto audioRender = audioDeck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 2, routedSnapshot);
    if (!require(audioRender.peakMagnitude > 0.01F && audioRender.assignedOutput == StereoOutputPair{2, 3} && audioRender.cueEnabled, "audio deck render through routing snapshot", output)) {
        return 1;
    }

    Vst3PluginManager pluginManager(persistence.pluginScanCache());
    PluginScanWorkerModel pluginWorker;
    const auto scanResult = pluginManager.scanOnBackgroundWorker(PluginScanDescriptor{14, {PluginScanCandidate{"Alpha Smoke Synth", "/alpha-plugins/alpha-smoke-synth.vst3"}}},
                                                                  pluginScanTicket(4),
                                                                  pluginWorker);
    if (!require(scanResult.ok() && scanResult.discovered.size() == 1 && pluginManager.formatManager().onlyVst3Enabled(), "mock VST3 scan", output)) {
        return 1;
    }
    Vst3PluginManager reloadedPlugins(persistence.pluginScanCache());
    if (!require(reloadedPlugins.loadCache() == PluginScanError::None && reloadedPlugins.knownPlugins().contains(pluginIdFromPath("/alpha-plugins/alpha-smoke-synth.vst3")), "plugin cache reload", output)) {
        return 1;
    }

    PluginGraphCommandModel pluginCommands;
    if (!require(pluginCommands.insertManualPlugin(graph, deck2, 0, PluginSlotRecoveryState::Available).ok() &&
                     pluginCommands.insertManualPlugin(graph, deck3, 0, PluginSlotRecoveryState::MissingPluginPlaceholder).ok(),
                 "plugin placeholder insertion queued",
                 output)) {
        return 1;
    }
    if (!require(graph.processPendingUpdatesOutsideCallback().ok(), "plugin placeholder graph update", output)) {
        return 1;
    }
    const auto pluginSnapshot = graph.captureSnapshotForAudioCallback();
    if (!require(pluginSnapshot.acceptsSequencerMidi(deck2, 0) && pluginSnapshot.deck(deck3).pluginSlots[0].state == PluginSlotState::MissingPluginPlaceholder, "hosted and missing plugin placeholders visible", output)) {
        return 1;
    }

    MidiStepSequencerDeck sequencer(deck2);
    sequencer.setPattern(deterministicSequencerPattern());
    sequencer.setOutputTarget(MidiOutputTarget{deck2, 0, true});
    const auto midiRender = sequencer.renderMidiBlock(AudioRenderConfiguration{44100, 6000}, runningClock(120.0, 0.0));
    const auto midiRoute = pluginSnapshot.routeSequencerMidi(deck2, midiRender.outputTarget.pluginSlotIndex, midiRender.midi.size());
    if (!require(midiRender.midi.size() > 0U && midiRoute.routed, "sequencer pattern routed to plugin placeholder", output)) {
        return 1;
    }

    MidiLearnController midiLearn;
    const auto learned = midiLearn.bind("routing.deck.1.output", MidiMessage::controlChange(0, 74, 100));
    if (!require(learned.status == MidiLearnStatus::Learned && persistence.midiMappings().save(learned.mapping).ok(), "MIDI learn mapping save", output)) {
        return 1;
    }
    MidiLearnController reloadedMidiLearn;
    const auto midiMappings = persistence.midiMappings().list();
    if (!require(midiMappings.ok(), "MIDI mappings reload list", output)) {
        return 1;
    }
    reloadedMidiLearn.loadMappings(midiMappings.value);
    reloadedMidiLearn.setDeviceConnected(false);
    const auto disconnectedDispatch = reloadedMidiLearn.dispatch(MidiMessage::controlChange(0, 74, 64));
    reloadedMidiLearn.setDeviceConnected(true);
    const auto reconnectedDispatch = reloadedMidiLearn.dispatch(MidiMessage::controlChange(0, 74, 64));
    if (!require(disconnectedDispatch.status == MidiDispatchStatus::DeviceDisconnected && reconnectedDispatch.dispatched(), "MIDI mapping survives disconnect reconnect", output)) {
        return 1;
    }

    const auto reloadedPreference = persistence.appPreferences().get("alpha.audio.backend");
    const auto reloadedRouting = persistence.routingConfig().load(deck1.index());
    if (!require(reloadedPreference.ok() && reloadedRouting.ok() && reloadedRouting.value.assignment.mainOutput == OutputBus::Output2, "preference and routing reload", output)) {
        return 1;
    }

    HybridUiShellModel uiShell;
    HybridUiShellInputSnapshot emptyUiInput;
    emptyUiInput.routing = AudioRoutingGraphSnapshot::createDefault(RoutingDeviceLayout::forChannelCount(4));
    emptyUiInput.midiLearn = MidiLearnIndicatorSnapshot{false, midiLearn.registry().size(), {}};
    const auto emptyUi = uiShell.buildSnapshot(emptyUiInput);
    if (!require(emptyUi.decks.size() == 4 && emptyUi.browser.empty && emptyUi.decks[0].waveform.placeholder, "empty UI shell placeholders", output)) {
        return 1;
    }

    HybridUiShellInputSnapshot populatedUiInput;
    populatedUiInput.browserTracks = browserTracks.value;
    populatedUiInput.routing = pluginSnapshot;
    populatedUiInput.midiLearn = MidiLearnIndicatorSnapshot{false, reloadedMidiLearn.mappingCount(), {}};
    const auto populatedUi = uiShell.buildSnapshot(populatedUiInput);
    if (!require(populatedUi.decks.size() == 4 && populatedUi.pluginChain.slots.size() == 20 && !populatedUi.decks[0].waveform.placeholder && populatedUi.decks[1].waveform.placeholder, "populated UI and unavailable waveform placeholders", output)) {
        return 1;
    }

    output << "alpha-smoke-summary: decks=4 imported=" << importResult.importedTracks.size()
           << " audio-peak=" << audioRender.peakMagnitude
           << " midi-events=" << midiRender.midi.size()
           << " plugin-slots=" << populatedUi.pluginChain.slots.size()
           << " midi-mappings=" << reloadedMidiLearn.mappingCount() << '\n';
    output << "alpha-smoke-test: ok\n";
    return 0;
}

}
