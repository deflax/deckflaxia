#include "audio/routing/AudioRoutingGraph.h"

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

int expectError(const deckflaxia::audio::routing::RoutingGraphResult& result,
                deckflaxia::audio::routing::RoutingGraphError error,
                const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == error, message + " should expose typed error");
}

int testMultiOutputGraph() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;

    AudioRoutingGraphController controller(RoutingDeviceLayout::forChannelCount(8));
    const auto initial = controller.captureSnapshotForAudioCallback();

    if (expect(initial.revision == 1, "initial graph should start at revision one") != 0) {
        return 1;
    }
    if (expect(initial.nodeCount == kMaxGraphNodes, "graph should model decks, plugin slots, buses, and outputs") != 0) {
        return 1;
    }
    if (expect(initial.connectionCount == 22, "default graph should deterministically connect each deck to master") != 0) {
        return 1;
    }

    for (const auto deckId : allDeckIds()) {
        const auto& deck = initial.deck(deckId);
        if (expect(deck.deckNodeId == deckNodeId(deckId), "deck node id should be deterministic") != 0) {
            return 1;
        }
        for (std::size_t slotIndex = 0; slotIndex < deck.pluginSlots.size(); ++slotIndex) {
            if (expect(deck.pluginSlots[slotIndex].id == pluginSlotNodeId(deckId, slotIndex), "plugin slot id should be deterministic") != 0) {
                return 1;
            }
            if (expect(deck.pluginSlots[slotIndex].placeholder, "plugin slots should be placeholders in T5") != 0) {
                return 1;
            }
        }
        if (expect(initial.hasConnection(deck.pluginSlots[3].id, initial.masterBusNode), "default deck tail should feed master") != 0) {
            return 1;
        }
    }

    if (expect(initial.cueBusNode == cueBusNodeId(), "cue bus node should be present") != 0) {
        return 1;
    }
    if (expect(initial.masterBusNode == masterBusNodeId(), "master bus node should be present") != 0) {
        return 1;
    }

    const auto deck0 = DeckId::fromIndex(0).value;
    const auto deck1 = DeckId::fromIndex(1).value;
    const auto deck2 = DeckId::fromIndex(2).value;
    if (expectOk(controller.enqueueAssignDeckOutput(deck0, OutputBus::Output4), "deck 1 output 4 assignment") != 0) {
        return 1;
    }
    if (expectOk(controller.enqueueAssignDeckOutput(deck1, OutputBus::Output2), "deck 2 output 2 assignment") != 0) {
        return 1;
    }
    if (expectOk(controller.enqueueSetCueEnabled(deck2, true), "deck 3 cue enable") != 0) {
        return 1;
    }
    if (expect(controller.pendingCommandCount() == 3, "commands should queue before graph update") != 0) {
        return 1;
    }
    if (expect(controller.activeSnapshot().deck(deck0).assignment.mainOutput == OutputBus::Master,
               "queued output assignment should not mutate active graph immediately") != 0) {
        return 1;
    }

    const auto processResult = controller.processPendingUpdatesOutsideCallback();
    if (expectOk(processResult, "outside-callback pending graph update") != 0) {
        return 1;
    }
    if (expect(processResult.warning == RoutingGraphWarning::None, "separate cue output should not warn") != 0) {
        return 1;
    }

    const auto updated = controller.captureSnapshotForAudioCallback();
    if (expect(updated.revision == 4, "three queued graph updates should advance revision deterministically") != 0) {
        return 1;
    }
    if (expect(updated.deck(deck0).assignment.mainOutput == OutputBus::Output4, "deck 1 should route to output 4") != 0) {
        return 1;
    }
    if (expect(updated.deck(deck0).assignedOutput == StereoOutputPair{6, 7}, "output 4 should map to channels 6/7") != 0) {
        return 1;
    }
    if (expect(updated.hasConnection(updated.deck(deck0).pluginSlots[3].id, updated.deviceOutputNodes[3]),
               "deck 1 chain should connect to output 4 node") != 0) {
        return 1;
    }
    if (expect(updated.hasConnection(updated.deck(deck1).pluginSlots[3].id, updated.deviceOutputNodes[1]),
               "deck 2 chain should connect to output 2 node") != 0) {
        return 1;
    }
    if (expect(updated.hasConnection(updated.deck(deck2).pluginSlots[3].id, updated.cueBusNode),
               "cue-enabled deck should connect to cue bus") != 0) {
        return 1;
    }
    return expect(updated.connectionCount == 23, "one cue send should add one graph connection");
}

int testTypedRoutingFailuresAndWarnings() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;

    AudioRoutingGraphController fourChannel(RoutingDeviceLayout::forChannelCount(4));
    const auto deck0 = DeckId::fromIndex(0).value;
    if (expectError(fourChannel.enqueueAssignDeckOutput(deck0, OutputBus::Output3),
                    RoutingGraphError::UnavailableOutputChannel,
                    "unavailable output 3 on four-channel device") != 0) {
        return 1;
    }
    if (expect(fourChannel.pendingCommandCount() == 0, "invalid output should not queue graph mutation") != 0) {
        return 1;
    }
    if (expect(fourChannel.activeSnapshot().revision == 1, "invalid output should leave graph revision unchanged") != 0) {
        return 1;
    }
    if (expectError(fourChannel.enqueueAssignDeckOutput(deck0, OutputBus::Cue),
                    RoutingGraphError::InvalidMainOutput,
                    "cue bus as main deck output") != 0) {
        return 1;
    }

    AudioRoutingGraphController stereo(RoutingDeviceLayout::forChannelCount(2));
    const auto cueResult = stereo.enqueueSetCueEnabled(deck0, true);
    if (expectOk(cueResult, "cue enable on stereo device") != 0) {
        return 1;
    }
    if (expect(cueResult.warning == RoutingGraphWarning::CueMasterOverlap, "cue/master overlap should warn at enqueue") != 0) {
        return 1;
    }
    const auto processResult = stereo.processPendingUpdatesOutsideCallback();
    if (expectOk(processResult, "stereo cue pending update") != 0) {
        return 1;
    }
    if (expect(processResult.warning == RoutingGraphWarning::CueMasterOverlap, "cue/master overlap should warn after update") != 0) {
        return 1;
    }
    if (expect(stereo.activeSnapshot().warning == RoutingGraphWarning::CueMasterOverlap, "snapshot should retain overlap warning") != 0) {
        return 1;
    }
    return expect(stereo.activeSnapshot().hasConnection(stereo.activeSnapshot().deck(deck0).pluginSlots[3].id,
                                                        stereo.activeSnapshot().cueBusNode),
                  "overlapping cue should still be represented in graph");
}

int testPendingUpdateDuringRender() {
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;

    AudioRoutingGraphController controller(RoutingDeviceLayout::forChannelCount(4));
    const auto deck3 = DeckId::fromIndex(3).value;
    const auto snapshotForCallback = controller.captureSnapshotForAudioCallback();

    if (expectOk(controller.enqueueAssignDeckOutput(deck3, OutputBus::Output2), "deck 4 output 2 queued assignment") != 0) {
        return 1;
    }

    const auto renderBeforeUpdate = controller.renderFromAudioCallback(snapshotForCallback);
    if (expect(renderBeforeUpdate.snapshotRevision == 1, "render should observe captured stable revision") != 0) {
        return 1;
    }
    if (expect(renderBeforeUpdate.pendingCommandCount == 1, "render should not consume pending graph update") != 0) {
        return 1;
    }
    if (expect(controller.activeSnapshot().deck(deck3).assignment.mainOutput == OutputBus::Master,
               "active graph should remain unchanged until outside-callback update") != 0) {
        return 1;
    }

    if (expectOk(controller.processPendingUpdatesOutsideCallback(), "outside-callback processing after render") != 0) {
        return 1;
    }

    const auto renderOldSnapshotAfterUpdate = controller.renderFromAudioCallback(snapshotForCallback);
    if (expect(renderOldSnapshotAfterUpdate.snapshotRevision == 1, "held callback snapshot should stay immutable after update") != 0) {
        return 1;
    }

    const auto updated = controller.captureSnapshotForAudioCallback();
    const auto renderAfterUpdate = controller.renderFromAudioCallback(updated);
    if (expect(renderAfterUpdate.snapshotRevision == 2, "new render should observe updated revision") != 0) {
        return 1;
    }
    if (expect(renderAfterUpdate.pendingCommandCount == 0, "outside-callback update should drain pending command") != 0) {
        return 1;
    }
    if (expect(updated.deck(deck3).assignment.mainOutput == OutputBus::Output2, "deck 4 should now route to output 2") != 0) {
        return 1;
    }
    return expect(updated.hasConnection(updated.deck(deck3).pluginSlots[3].id, updated.deviceOutputNodes[1]),
                  "updated graph should connect deck 4 to output 2");
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "multiout") {
        return testMultiOutputGraph();
    }
    if (filter == "errors") {
        return testTypedRoutingFailuresAndWarnings();
    }
    if (filter == "pending") {
        return testPendingUpdateDuringRender();
    }

    if (testMultiOutputGraph() != 0 || testTypedRoutingFailuresAndWarnings() != 0 || testPendingUpdateDuringRender() != 0) {
        return 1;
    }

    std::cout << "Audio routing graph tests passed\n";
    return 0;
}
