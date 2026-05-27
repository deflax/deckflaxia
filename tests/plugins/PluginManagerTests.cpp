#include "audio/routing/AudioRoutingGraph.h"
#include "decks/SequencerDeck.h"
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

int expectOk(deckflaxia::plugins::PluginScanError error, const std::string& message) {
    return expect(error == deckflaxia::plugins::PluginScanError::None, message + " should succeed");
}

int expectOk(const deckflaxia::plugins::PluginScanResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const deckflaxia::audio::routing::RoutingGraphResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

deckflaxia::core::BackgroundJobTicket pluginScanTicket(std::uint64_t id) {
    return deckflaxia::core::BackgroundJobTicket{id, deckflaxia::core::BackgroundJobKind::ScanPlugins, deckflaxia::core::BackgroundWorkerRole::PluginScanWorker};
}

deckflaxia::core::MasterClockState runningClock(double bpm, double positionBeats) {
    auto clock = deckflaxia::core::MasterClockState::stopped(bpm);
    clock.positionBeats = positionBeats;
    clock.start();
    return clock;
}

deckflaxia::core::MidiStepPattern singleStepPattern() {
    auto pattern = deckflaxia::core::MidiStepPattern::sixteenStepDefault(60);
    pattern.steps[0] = deckflaxia::core::MidiStep{true, 60, 100, 0.25};
    return pattern;
}

int testScanCache() {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::plugins;

    PersistenceService service;
    Vst3PluginManager manager(service.pluginScanCache());
    if (expect(manager.formatManager().onlyVst3Enabled(), "format manager should register only the alpha plugin format") != 0) {
        return 1;
    }
    if (expect(manager.formatManager().formatCount() == 1, "format manager should expose exactly one format") != 0) {
        return 1;
    }

    PluginScanWorkerModel worker;
    const PluginScanDescriptor scan{1, {PluginScanCandidate{"Alpha Synth", "/plugins/alpha-synth.vst3"}}};
    const auto scanResult = manager.scanOnBackgroundWorker(scan, pluginScanTicket(1), worker);
    if (expectOk(scanResult, "background plugin scan") != 0) {
        return 1;
    }
    if (expect(worker.scheduled(), "scan should schedule through plugin scan worker") != 0) {
        return 1;
    }
    if (expect(scanResult.discovered.size() == 1, "valid plugin candidate should be discovered") != 0) {
        return 1;
    }
    if (expect(manager.knownPlugins().contains(pluginIdFromPath("/plugins/alpha-synth.vst3")), "manager should know scanned plugin") != 0) {
        return 1;
    }

    Vst3PluginManager reloaded(service.pluginScanCache());
    if (expectOk(reloaded.loadCache(), "plugin scan cache reload") != 0) {
        return 1;
    }
    if (expect(reloaded.knownPlugins().contains(pluginIdFromPath("/plugins/alpha-synth.vst3")), "scan cache should persist discovered plugin") != 0) {
        return 1;
    }

    const PluginChainDescriptor savedChain{"deck-1-chain",
                                           {deckflaxia::core::PluginDescriptor{pluginIdFromPath("/plugins/alpha-synth.vst3"), "Alpha Synth", false},
                                            deckflaxia::core::PluginDescriptor{"vst3:/plugins/missing-synth.vst3", "Missing Synth", false}}};
    const auto recovery = reloaded.recoverSavedPluginChain(savedChain);
    if (expect(recovery.slots.size() == 2, "saved chain recovery should preserve slot count") != 0) {
        return 1;
    }
    if (expect(recovery.slots[0].state == PluginSlotRecoveryState::Available, "available saved plugin should recover as available") != 0) {
        return 1;
    }
    if (expect(recovery.slots[1].state == PluginSlotRecoveryState::MissingPluginPlaceholder, "missing saved plugin should become recoverable placeholder") != 0) {
        return 1;
    }

    std::cout << "Plugin.ScanCache discovered=" << scanResult.discovered.size() << " formats=" << manager.formatManager().formatCount() << '\n';
    return 0;
}

int testBlacklist() {
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::plugins;

    PersistenceService service;
    Vst3PluginManager manager(service.pluginScanCache());
    PluginScanWorkerModel worker;
    const PluginScanDescriptor malformed{2, {PluginScanCandidate{"Broken", "/plugins/broken.binary"}}};
    const auto firstScan = manager.scanOnBackgroundWorker(malformed, pluginScanTicket(2), worker);
    if (expectOk(firstScan, "malformed plugin scan") != 0) {
        return 1;
    }
    if (expect(firstScan.discovered.empty(), "malformed candidate should not be discovered") != 0) {
        return 1;
    }
    if (expect(firstScan.blacklistedPaths.size() == 1, "malformed candidate should be blacklisted") != 0) {
        return 1;
    }

    Vst3PluginManager reloaded(service.pluginScanCache());
    if (expectOk(reloaded.loadCache(), "blacklist cache reload") != 0) {
        return 1;
    }
    if (expect(reloaded.knownPlugins().isPathBlacklisted("/plugins/broken.binary"), "blacklist should persist failed path") != 0) {
        return 1;
    }

    PluginScanWorkerModel secondWorker;
    const auto secondScan = reloaded.scanOnBackgroundWorker(malformed, pluginScanTicket(3), secondWorker);
    if (expectOk(secondScan, "blacklisted path rescan") != 0) {
        return 1;
    }
    if (expect(secondScan.discovered.empty() && secondScan.blacklistedPaths.size() == 1,
               "blacklisted path should be skipped on later scan") != 0) {
        return 1;
    }

    const auto records = service.pluginScanCache().list();
    if (expect(records.ok() && records.value.size() == 1 && records.value[0].blacklisted, "repository should expose persisted blacklist") != 0) {
        return 1;
    }

    PluginScanWorkerModel cancelledWorker;
    cancelledWorker.requestStopAfterCandidateCount(1);
    const auto cancelled = reloaded.scanOnBackgroundWorker(PluginScanDescriptor{4,
                                                                                {PluginScanCandidate{"Later", "/plugins/later.vst3"},
                                                                                 PluginScanCandidate{"Skipped", "/plugins/skipped.vst3"}}},
                                                          pluginScanTicket(4),
                                                          cancelledWorker);
    if (expectOk(cancelled, "cancelled scan request") != 0) {
        return 1;
    }
    if (expect(cancelled.cancelled && cancelled.discovered.size() == 1, "scan should honor cancellation between candidates") != 0) {
        return 1;
    }

    std::cout << "Plugin.Blacklist paths=" << secondScan.blacklistedPaths.size() << " repository-records=" << records.value.size() << '\n';
    return 0;
}

int testSequencerMidiRouting() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::plugins;

    PersistenceService service;
    Vst3PluginManager manager(service.pluginScanCache());
    PluginScanWorkerModel worker;
    const auto scanResult = manager.scanOnBackgroundWorker(PluginScanDescriptor{5, {PluginScanCandidate{"Alpha Synth", "/plugins/alpha-synth.vst3"}}},
                                                           pluginScanTicket(5),
                                                           worker);
    if (expectOk(scanResult, "synth plugin placeholder scan") != 0) {
        return 1;
    }

    AudioRoutingGraphController graph(RoutingDeviceLayout::forChannelCount(8));
    PluginGraphCommandModel graphCommands;
    const auto deckId = DeckId::fromIndex(0).value;
    if (expectOk(graphCommands.insertManualPlugin(graph, deckId, 2, PluginSlotRecoveryState::Available), "manual plugin chain insertion") != 0) {
        return 1;
    }
    if (expect(graph.pendingCommandCount() == 1, "plugin insertion should queue before graph update") != 0) {
        return 1;
    }
    if (expect(!graph.captureSnapshotForAudioCallback().acceptsSequencerMidi(deckId, 2), "pre-update snapshot should not accept MIDI") != 0) {
        return 1;
    }
    if (expectOk(graph.processPendingUpdatesOutsideCallback(), "outside-callback plugin graph update") != 0) {
        return 1;
    }

    MidiStepSequencerDeck sequencer(deckId);
    sequencer.setPattern(singleStepPattern());
    sequencer.setOutputTarget(MidiOutputTarget{deckId, 2, true});
    const auto midiRender = sequencer.renderMidiBlock(AudioRenderConfiguration{44100, 6000}, runningClock(120.0, 0.0));
    if (expect(midiRender.midi.size() > 0, "sequencer should produce MIDI events") != 0) {
        return 1;
    }

    const auto snapshot = graph.captureSnapshotForAudioCallback();
    GraphNodeId destination{};
    if (expect(snapshot.resolvePluginSlotNode(deckId, midiRender.outputTarget.pluginSlotIndex, destination), "MIDI target slot should resolve to graph node") != 0) {
        return 1;
    }
    const auto route = snapshot.routeSequencerMidi(deckId, midiRender.outputTarget.pluginSlotIndex, midiRender.midi.size());
    if (expect(route.routed && route.destination == destination && route.eventCount == midiRender.midi.size(),
               "sequencer MIDI should route to hosted plugin placeholder node") != 0) {
        return 1;
    }

    if (expectOk(graphCommands.removeManualPlugin(graph, deckId, 2), "manual plugin chain removal") != 0) {
        return 1;
    }
    if (expectOk(graph.processPendingUpdatesOutsideCallback(), "outside-callback plugin removal update") != 0) {
        return 1;
    }
    if (expect(!graph.captureSnapshotForAudioCallback().acceptsSequencerMidi(deckId, 2), "removed plugin slot should stop accepting MIDI") != 0) {
        return 1;
    }

    std::cout << "Plugin.SequencerMidiRouting events=" << midiRender.midi.size() << " node=" << route.destination.value << '\n';
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "scan-cache") {
        return testScanCache();
    }
    if (filter == "blacklist") {
        return testBlacklist();
    }
    if (filter == "midi-routing") {
        return testSequencerMidiRouting();
    }

    if (testScanCache() != 0 || testBlacklist() != 0 || testSequencerMidiRouting() != 0) {
        return 1;
    }

    std::cout << "Plugin manager tests passed\n";
    return 0;
}
