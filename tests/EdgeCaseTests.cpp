#include "app/UiShell.h"
#include "audio/AudioEngine.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"
#include "library/LibraryModel.h"
#include "midi/MidiLearn.h"
#include "persistence/Persistence.h"
#include "plugins/PluginManager.h"

#include <iostream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int expectOk(const deckflaxia::audio::routing::RoutingGraphResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

deckflaxia::core::BackgroundJobTicket databaseTicket(std::uint64_t id) {
    return deckflaxia::core::BackgroundJobTicket{id, deckflaxia::core::BackgroundJobKind::PersistLibraryChange, deckflaxia::core::BackgroundWorkerRole::DatabaseWorker};
}

deckflaxia::core::BackgroundJobTicket pluginScanTicket(std::uint64_t id) {
    return deckflaxia::core::BackgroundJobTicket{id, deckflaxia::core::BackgroundJobKind::ScanPlugins, deckflaxia::core::BackgroundWorkerRole::PluginScanWorker};
}

deckflaxia::library::ProLibraryRepository makeLibrary(deckflaxia::persistence::PersistenceService& service) {
    return deckflaxia::library::ProLibraryRepository{service.libraryTracks(), service.crates(), service.playlists(), service.trackMetadata()};
}

int testDisconnectedDevicesAndSampleRateChanges() {
    using namespace deckflaxia::audio;

    auto service = AudioDeviceService(backendPolicyForPlatform(HostAudioPlatform::Linux));
    service.applyEvent(AudioDeviceEventKind::DeviceRunning, AudioRenderConfiguration{44100, 128});
    if (expect(service.state().status == AudioDeviceConnectionStatus::Running, "device starts running") != 0) {
        return 1;
    }
    service.applyEvent(AudioDeviceEventKind::SampleRateChanged, AudioRenderConfiguration{48000, 512});
    if (expect(service.state().status == AudioDeviceConnectionStatus::SampleRateChanged && service.state().sampleRateHz == 48000 && service.state().bufferFrames == 512,
               "sample-rate change updates typed state") != 0) {
        return 1;
    }
    service.applyEvent(AudioDeviceEventKind::DeviceDisconnected, AudioRenderConfiguration{48000, 512});
    return expect(service.state().status == AudioDeviceConnectionStatus::Disconnected && service.state().degraded, "disconnect is degraded typed state");
}

int testUnavailableOutputs() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;

    AudioRoutingGraphController graph(RoutingDeviceLayout::forChannelCount(4));
    const auto deck = DeckId::fromIndex(0).value;
    const auto unavailable = graph.enqueueAssignDeckOutput(deck, OutputBus::Output3);
    if (expect(!unavailable.ok() && unavailable.error == RoutingGraphError::UnavailableOutputChannel, "unavailable output returns typed error") != 0) {
        return 1;
    }
    return expect(graph.pendingCommandCount() == 0 && graph.activeSnapshot().revision == 1, "unavailable output does not mutate graph");
}

int testMissingPlugins() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::plugins;

    PersistenceService persistence;
    Vst3PluginManager manager(persistence.pluginScanCache());
    const PluginChainDescriptor savedChain{"edge-chain", {deckflaxia::core::PluginDescriptor{"vst3:/missing/edge.vst3", "Missing Edge", false}}};
    const auto recovery = manager.recoverSavedPluginChain(savedChain);
    if (expect(recovery.slots.size() == 1 && recovery.slots[0].state == PluginSlotRecoveryState::MissingPluginPlaceholder,
               "missing saved plugin recovers as placeholder") != 0) {
        return 1;
    }

    AudioRoutingGraphController graph(RoutingDeviceLayout::forChannelCount(8));
    PluginGraphCommandModel commands;
    const auto deck = DeckId::fromIndex(0).value;
    if (expectOk(commands.insertManualPlugin(graph, deck, 0, recovery.slots[0].state), "missing plugin placeholder insertion") != 0) {
        return 1;
    }
    if (expectOk(graph.processPendingUpdatesOutsideCallback(), "missing plugin outside-callback graph update") != 0) {
        return 1;
    }
    return expect(graph.captureSnapshotForAudioCallback().deck(deck).pluginSlots[0].state == PluginSlotState::MissingPluginPlaceholder,
                  "snapshot exposes missing plugin placeholder");
}

int testDeletedTracks() {
    using namespace deckflaxia::library;
    using namespace deckflaxia::persistence;

    PersistenceService persistence;
    auto library = makeLibrary(persistence);
    LibraryScanWorkerModel worker;
    const auto imported = library.importFolderOnBackgroundWorker(FolderImportRequest{1, "edge-crate", "Edge", {FilesystemEntry{"/edge/deleted.wav", true}}}, databaseTicket(1), worker);
    if (expect(imported.ok() && imported.importedTracks.size() == 1, "edge track import") != 0) {
        return 1;
    }
    if (expect(library.markTrackMissingByPath("/edge/deleted.wav").ok(), "deleted path marks track missing") != 0) {
        return 1;
    }
    const auto track = library.findBrowserTrack(trackIdFromPath("/edge/deleted.wav"));
    return expect(track.ok() && track.value.availability == TrackAvailability::Missing, "deleted track remains in library as missing");
}

int testCancelledScans() {
    using namespace deckflaxia::library;
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::plugins;

    PersistenceService persistence;
    Vst3PluginManager pluginManager(persistence.pluginScanCache());
    PluginScanWorkerModel pluginWorker;
    pluginWorker.requestStopAfterCandidateCount(1);
    const auto pluginScan = pluginManager.scanOnBackgroundWorker(PluginScanDescriptor{2,
                                                                                      {PluginScanCandidate{"One", "/edge/one.vst3"},
                                                                                       PluginScanCandidate{"Two", "/edge/two.vst3"}}},
                                                                 pluginScanTicket(2),
                                                                 pluginWorker);
    if (expect(pluginScan.ok() && pluginScan.cancelled && pluginScan.discovered.size() == 1, "plugin scan cancels between candidates") != 0) {
        return 1;
    }

    auto library = makeLibrary(persistence);
    LibraryScanWorkerModel libraryWorker;
    libraryWorker.requestStopAfterEntryCount(2);
    const auto libraryScan = library.importFolderOnBackgroundWorker(FolderImportRequest{3,
                                                                                        "cancel-crate",
                                                                                        "Cancel",
                                                                                        {FilesystemEntry{"/edge/a.wav", true},
                                                                                         FilesystemEntry{"/edge/b.wav", true},
                                                                                         FilesystemEntry{"/edge/c.wav", true}}},
                                                                   databaseTicket(3),
                                                                   libraryWorker);
    return expect(libraryScan.ok() && libraryScan.cancelled && libraryScan.importedTracks.size() == 2, "library scan cancels between entries");
}

int testUnavailableWaveformData() {
    using namespace deckflaxia::app;
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::library;

    BrowserTrackEntry track;
    track.track = LibraryTrack{"track:no-waveform", "No Waveform", "Edge", BeatgridMetadata::fromBpm(120.0, 0.0).value, MusicalKey::Unknown};
    track.path = "/edge/no-waveform.wav";

    HybridUiShellInputSnapshot input;
    input.browserTracks.push_back(track);
    input.routing = AudioRoutingGraphSnapshot::createDefault(RoutingDeviceLayout::forChannelCount(4));
    input.midiLearn = MidiLearnIndicatorSnapshot{false, 1, {}};

    const HybridUiShellModel shell;
    const auto snapshot = shell.buildSnapshot(input);
    return expect(snapshot.decks[0].waveform.placeholder && snapshot.browser.tracks[0].waveformAvailable == false,
                  "unavailable waveform renders placeholder without mutable engine access");
}

int testGraphUpdateDuringRender() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;

    AudioRoutingGraphController graph(RoutingDeviceLayout::forChannelCount(4));
    const auto deck = DeckId::fromIndex(3).value;
    const auto callbackSnapshot = graph.captureSnapshotForAudioCallback();
    if (expectOk(graph.enqueueAssignDeckOutput(deck, OutputBus::Output2), "queued graph update during render") != 0) {
        return 1;
    }
    const auto renderBefore = graph.renderFromAudioCallback(callbackSnapshot);
    if (expect(renderBefore.snapshotRevision == 1 && renderBefore.pendingCommandCount == 1, "render observes immutable old snapshot") != 0) {
        return 1;
    }
    if (expectOk(graph.processPendingUpdatesOutsideCallback(), "outside callback update after render") != 0) {
        return 1;
    }
    const auto renderOld = graph.renderFromAudioCallback(callbackSnapshot);
    const auto updated = graph.captureSnapshotForAudioCallback();
    const auto renderNew = graph.renderFromAudioCallback(updated);
    return expect(renderOld.snapshotRevision == 1 && renderNew.snapshotRevision == 2 && renderNew.pendingCommandCount == 0,
                  "graph update does not mutate held render snapshot");
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "devices") {
        return testDisconnectedDevicesAndSampleRateChanges();
    }
    if (filter == "outputs") {
        return testUnavailableOutputs();
    }
    if (filter == "plugins") {
        return testMissingPlugins();
    }
    if (filter == "deleted-tracks") {
        return testDeletedTracks();
    }
    if (filter == "cancelled-scans") {
        return testCancelledScans();
    }
    if (filter == "waveform") {
        return testUnavailableWaveformData();
    }
    if (filter == "graph-render") {
        return testGraphUpdateDuringRender();
    }

    if (testDisconnectedDevicesAndSampleRateChanges() != 0 || testUnavailableOutputs() != 0 || testMissingPlugins() != 0 ||
        testDeletedTracks() != 0 || testCancelledScans() != 0 || testUnavailableWaveformData() != 0 || testGraphUpdateDuringRender() != 0) {
        return 1;
    }

    std::cout << "EdgeCases tests passed\n";
    return 0;
}
