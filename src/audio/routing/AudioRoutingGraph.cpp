#include "audio/routing/AudioRoutingGraph.h"

namespace deckflaxia::audio::routing {

namespace {

constexpr std::uint32_t kDeckNodeBase = 100;
constexpr std::uint32_t kPluginNodeBase = 200;
constexpr std::uint32_t kMasterBusNode = 900;
constexpr std::uint32_t kCueBusNode = 901;
constexpr std::uint32_t kDeviceOutputNodeBase = 1000;

std::uint32_t encodeOutputBus(core::OutputBus output) noexcept {
    return static_cast<std::uint32_t>(output);
}

core::OutputBus decodeOutputBus(std::uint32_t value) noexcept {
    return static_cast<core::OutputBus>(value);
}

RoutingGraphWarning mergedWarning(RoutingGraphWarning left, RoutingGraphWarning right) noexcept {
    if (left == RoutingGraphWarning::CueMasterOverlap || right == RoutingGraphWarning::CueMasterOverlap) {
        return RoutingGraphWarning::CueMasterOverlap;
    }
    return RoutingGraphWarning::None;
}

bool validDeckId(core::DeckId deckId) noexcept {
    return deckId.index() < kDeckCount;
}

void addNode(AudioRoutingGraphSnapshot& snapshot, RoutingGraphNode node) noexcept {
    snapshot.nodes[snapshot.nodeCount] = node;
    ++snapshot.nodeCount;
}

void addConnection(AudioRoutingGraphSnapshot& snapshot, GraphNodeId source, GraphNodeId destination) noexcept {
    snapshot.connections[snapshot.connectionCount] = RoutingGraphConnection{source, destination};
    ++snapshot.connectionCount;
}

GraphNodeId deckTailNodeId(const DeckRoutingNode& deck) noexcept {
    return deck.pluginSlots[kPluginSlotsPerDeck - 1].id;
}

void rebuildSnapshot(AudioRoutingGraphSnapshot& snapshot) noexcept {
    snapshot.nodeCount = 0;
    snapshot.connectionCount = 0;
    snapshot.warning = RoutingGraphWarning::None;
    snapshot.cueBusNode = cueBusNodeId();
    snapshot.masterBusNode = masterBusNodeId();

    for (std::size_t outputIndex = 0; outputIndex < snapshot.deviceOutputNodes.size(); ++outputIndex) {
        snapshot.deviceOutputNodes[outputIndex] = deviceOutputNodeId(outputIndex);
    }

    for (const auto deckId : core::allDeckIds()) {
        auto& deck = snapshot.decks[deckId.index()];
        deck.deckId = deckId;
        deck.deckNodeId = deckNodeId(deckId);
        for (std::size_t slotIndex = 0; slotIndex < deck.pluginSlots.size(); ++slotIndex) {
            auto& slot = deck.pluginSlots[slotIndex];
            slot.id = pluginSlotNodeId(deckId, slotIndex);
            slot.slotIndex = static_cast<std::uint8_t>(slotIndex);
            slot.placeholder = true;
        }

        addNode(snapshot, RoutingGraphNode{deck.deckNodeId, RoutingGraphNodeKind::Deck, static_cast<std::uint8_t>(deckId.index()), 0, {}});
        for (const auto& slot : deck.pluginSlots) {
            addNode(snapshot,
                    RoutingGraphNode{slot.id,
                                     RoutingGraphNodeKind::PluginSlotPlaceholder,
                                     static_cast<std::uint8_t>(deckId.index()),
                                     slot.slotIndex,
                                     {}});
        }
    }

    addNode(snapshot, RoutingGraphNode{snapshot.cueBusNode, RoutingGraphNodeKind::CueBus, 0, 0, snapshot.layout.cueOutput});
    addNode(snapshot, RoutingGraphNode{snapshot.masterBusNode, RoutingGraphNodeKind::MasterBus, 0, 0, snapshot.layout.masterOutput});
    for (std::size_t outputIndex = 0; outputIndex < snapshot.deviceOutputNodes.size(); ++outputIndex) {
        addNode(snapshot,
                RoutingGraphNode{snapshot.deviceOutputNodes[outputIndex],
                                 RoutingGraphNodeKind::DeviceOutputPair,
                                 0,
                                 static_cast<std::uint8_t>(outputIndex),
                                 snapshot.layout.deckOutputs[outputIndex]});
    }

    for (const auto& deck : snapshot.decks) {
        addConnection(snapshot, deck.deckNodeId, deck.pluginSlots[0].id);
        for (std::size_t slotIndex = 1; slotIndex < deck.pluginSlots.size(); ++slotIndex) {
            addConnection(snapshot, deck.pluginSlots[slotIndex - 1].id, deck.pluginSlots[slotIndex].id);
        }

        const auto tail = deckTailNodeId(deck);
        if (deck.assignment.mainOutput == core::OutputBus::Master) {
            addConnection(snapshot, tail, snapshot.masterBusNode);
        } else {
            const auto outputIndex = static_cast<std::size_t>(deck.assignment.mainOutput) - static_cast<std::size_t>(core::OutputBus::Output1);
            addConnection(snapshot, tail, snapshot.deviceOutputNodes[outputIndex]);
        }

        if (deck.assignment.cueEnabled) {
            addConnection(snapshot, tail, snapshot.cueBusNode);
            snapshot.warning = mergedWarning(snapshot.warning, snapshot.layout.cueWarning());
        }
    }

    addConnection(snapshot, snapshot.masterBusNode, snapshot.deviceOutputNodes[0]);
    const std::size_t cueOutputIndex = snapshot.layout.cueOutput == snapshot.layout.deckOutputs[1] ? 1 : 0;
    addConnection(snapshot, snapshot.cueBusNode, snapshot.deviceOutputNodes[cueOutputIndex]);
}

} // namespace

RoutingDeviceLayout RoutingDeviceLayout::forChannelCount(std::uint32_t channelCount) noexcept {
    RoutingDeviceLayout layout;
    layout.channelCount = channelCount;
    layout.masterOutput = StereoOutputPair{0, 1};
    layout.cueOutput = channelCount >= 4 ? StereoOutputPair{2, 3} : StereoOutputPair{0, 1};
    return layout;
}

bool RoutingDeviceLayout::isAvailable(StereoOutputPair pair) const noexcept {
    return pair.fits(channelCount);
}

RoutingGraphWarning RoutingDeviceLayout::cueWarning() const noexcept {
    if (cueOutput == masterOutput) {
        return RoutingGraphWarning::CueMasterOverlap;
    }
    return RoutingGraphWarning::None;
}

AudioRoutingGraphSnapshot AudioRoutingGraphSnapshot::createDefault(RoutingDeviceLayout layout) noexcept {
    AudioRoutingGraphSnapshot snapshot;
    snapshot.revision = 1;
    snapshot.layout = layout;
    const auto ids = core::allDeckIds();
    for (std::size_t index = 0; index < snapshot.decks.size(); ++index) {
        StereoOutputPair output{};
        const auto outputResult = outputPairForBus(layout, core::OutputBus::Master, output);
        if (!outputResult.ok()) {
            output = layout.masterOutput;
        }
        snapshot.decks[index].deckId = ids[index];
        snapshot.decks[index].assignment = core::RoutingAssignment{core::OutputBus::Master, false};
        snapshot.decks[index].assignedOutput = output;
    }
    rebuildSnapshot(snapshot);
    return snapshot;
}

const DeckRoutingNode& AudioRoutingGraphSnapshot::deck(core::DeckId id) const noexcept {
    return decks[id.index()];
}

bool AudioRoutingGraphSnapshot::hasConnection(GraphNodeId source, GraphNodeId destination) const noexcept {
    for (std::size_t index = 0; index < connectionCount; ++index) {
        if (connections[index].source == source && connections[index].destination == destination) {
            return true;
        }
    }
    return false;
}

bool AudioRoutingGraphSnapshot::resolvePluginSlotNode(core::DeckId deckId, std::size_t slotIndex, GraphNodeId& nodeId) const noexcept {
    if (!validDeckId(deckId) || slotIndex >= kPluginSlotsPerDeck) {
        return false;
    }
    nodeId = decks[deckId.index()].pluginSlots[slotIndex].id;
    return true;
}

bool AudioRoutingGraphSnapshot::acceptsSequencerMidi(core::DeckId deckId, std::size_t slotIndex) const noexcept {
    if (!validDeckId(deckId) || slotIndex >= kPluginSlotsPerDeck) {
        return false;
    }
    const auto state = decks[deckId.index()].pluginSlots[slotIndex].state;
    return state == PluginSlotState::HostedPluginPlaceholder || state == PluginSlotState::MissingPluginPlaceholder;
}

SequencerMidiRoute AudioRoutingGraphSnapshot::routeSequencerMidi(core::DeckId deckId,
                                                                 std::size_t slotIndex,
                                                                 std::size_t eventCount) const noexcept {
    GraphNodeId destination{};
    if (!resolvePluginSlotNode(deckId, slotIndex, destination) || !acceptsSequencerMidi(deckId, slotIndex) || eventCount == 0U) {
        return SequencerMidiRoute{destination, eventCount, false};
    }
    return SequencerMidiRoute{destination, eventCount, true};
}

bool FixedRoutingCommandQueue::tryPushFromMessageThread(const AudioGraphCommand& command) noexcept {
    if (writeIndex_ == commands_.size()) {
        return false;
    }
    commands_[writeIndex_] = command;
    ++writeIndex_;
    return true;
}

bool FixedRoutingCommandQueue::tryPopForAudioThread(AudioGraphCommand& command) noexcept {
    if (audioReadIndex_ == writeIndex_) {
        return false;
    }
    command = commands_[audioReadIndex_];
    ++audioReadIndex_;
    return true;
}

bool FixedRoutingCommandQueue::tryPopForGraphUpdate(AudioGraphCommand& command) noexcept {
    if (updateReadIndex_ == writeIndex_) {
        return false;
    }
    command = commands_[updateReadIndex_];
    ++updateReadIndex_;
    if (audioReadIndex_ < updateReadIndex_) {
        audioReadIndex_ = updateReadIndex_;
    }
    if (updateReadIndex_ == writeIndex_) {
        writeIndex_ = 0;
        audioReadIndex_ = 0;
        updateReadIndex_ = 0;
    }
    return true;
}

std::size_t FixedRoutingCommandQueue::pendingCount() const noexcept {
    return writeIndex_ - updateReadIndex_;
}

AudioRoutingGraphController::AudioRoutingGraphController(RoutingDeviceLayout layout) noexcept
    : activeSnapshot_(AudioRoutingGraphSnapshot::createDefault(layout)) {}

const AudioRoutingGraphSnapshot& AudioRoutingGraphController::activeSnapshot() const noexcept {
    return activeSnapshot_;
}

AudioRoutingGraphSnapshot AudioRoutingGraphController::captureSnapshotForAudioCallback() const noexcept {
    return activeSnapshot_;
}

std::size_t AudioRoutingGraphController::pendingCommandCount() const noexcept {
    return commands_.pendingCount();
}

FixedRoutingCommandQueue& AudioRoutingGraphController::commandQueue() noexcept {
    return commands_;
}

RoutingGraphResult AudioRoutingGraphController::enqueueAssignDeckOutput(core::DeckId deckId, core::OutputBus output) noexcept {
    const auto validation = validateDeckOutput(deckId, output);
    if (!validation.ok()) {
        return validation;
    }
    if (!commands_.tryPushFromMessageThread(AudioGraphCommand{AudioGraphCommandKind::AssignDeckOutput,
                                                              static_cast<std::uint32_t>(deckId.index()),
                                                              0.0F,
                                                              encodeOutputBus(output)})) {
        return RoutingGraphResult::failure(RoutingGraphError::CommandQueueFull);
    }
    return validation;
}

RoutingGraphResult AudioRoutingGraphController::enqueueSetCueEnabled(core::DeckId deckId, bool enabled) noexcept {
    if (!validDeckId(deckId)) {
        return RoutingGraphResult::failure(RoutingGraphError::InvalidDeckId);
    }
    if (!commands_.tryPushFromMessageThread(AudioGraphCommand{AudioGraphCommandKind::SetCueEnabled,
                                                              static_cast<std::uint32_t>(deckId.index()),
                                                              enabled ? 1.0F : 0.0F,
                                                              0})) {
        return RoutingGraphResult::failure(RoutingGraphError::CommandQueueFull);
    }
    if (enabled) {
        return RoutingGraphResult::success(activeSnapshot_.layout.cueWarning());
    }
    return RoutingGraphResult::success();
}

RoutingGraphResult AudioRoutingGraphController::enqueueInsertPluginSlot(core::DeckId deckId, std::size_t slotIndex, bool missingPlugin) noexcept {
    const auto validation = validatePluginSlot(deckId, slotIndex);
    if (!validation.ok()) {
        return validation;
    }
    if (!commands_.tryPushFromMessageThread(AudioGraphCommand{AudioGraphCommandKind::InsertPluginSlot,
                                                              static_cast<std::uint32_t>(deckId.index()),
                                                              missingPlugin ? 1.0F : 0.0F,
                                                              static_cast<std::uint32_t>(slotIndex)})) {
        return RoutingGraphResult::failure(RoutingGraphError::CommandQueueFull);
    }
    return RoutingGraphResult::success();
}

RoutingGraphResult AudioRoutingGraphController::enqueueRemovePluginSlot(core::DeckId deckId, std::size_t slotIndex) noexcept {
    const auto validation = validatePluginSlot(deckId, slotIndex);
    if (!validation.ok()) {
        return validation;
    }
    if (!commands_.tryPushFromMessageThread(AudioGraphCommand{AudioGraphCommandKind::RemovePluginSlot,
                                                              static_cast<std::uint32_t>(deckId.index()),
                                                              0.0F,
                                                              static_cast<std::uint32_t>(slotIndex)})) {
        return RoutingGraphResult::failure(RoutingGraphError::CommandQueueFull);
    }
    return RoutingGraphResult::success();
}

RoutingGraphResult AudioRoutingGraphController::processPendingUpdatesOutsideCallback() noexcept {
    RoutingGraphResult result = RoutingGraphResult::success();
    AudioGraphCommand command{};
    while (commands_.tryPopForGraphUpdate(command)) {
        const auto commandResult = applyCommand(command);
        if (!commandResult.ok()) {
            return commandResult;
        }
        result.warning = mergedWarning(result.warning, commandResult.warning);
    }
    return result;
}

RoutingRenderResult AudioRoutingGraphController::renderFromAudioCallback(const AudioRoutingGraphSnapshot& snapshot) const noexcept {
    return RoutingRenderResult{snapshot.revision, snapshot.nodeCount, snapshot.connectionCount, commands_.pendingCount()};
}

RoutingGraphResult AudioRoutingGraphController::validateDeckOutput(core::DeckId deckId, core::OutputBus output) const noexcept {
    if (!validDeckId(deckId)) {
        return RoutingGraphResult::failure(RoutingGraphError::InvalidDeckId);
    }
    StereoOutputPair pair{};
    const auto pairResult = outputPairForBus(activeSnapshot_.layout, output, pair);
    if (!pairResult.ok()) {
        return pairResult;
    }
    if (!activeSnapshot_.layout.isAvailable(pair)) {
        return RoutingGraphResult::failure(RoutingGraphError::UnavailableOutputChannel);
    }
    return pairResult;
}

RoutingGraphResult AudioRoutingGraphController::validatePluginSlot(core::DeckId deckId, std::size_t slotIndex) const noexcept {
    if (!validDeckId(deckId)) {
        return RoutingGraphResult::failure(RoutingGraphError::InvalidDeckId);
    }
    if (slotIndex >= kPluginSlotsPerDeck) {
        return RoutingGraphResult::failure(RoutingGraphError::InvalidPluginSlot);
    }
    return RoutingGraphResult::success();
}

RoutingGraphResult AudioRoutingGraphController::applyCommand(AudioGraphCommand command) noexcept {
    if (command.targetId >= kDeckCount) {
        return RoutingGraphResult::failure(RoutingGraphError::InvalidDeckId);
    }
    const auto deckId = core::DeckId::fromIndex(command.targetId).value;
    auto next = activeSnapshot_;
    RoutingGraphWarning warning = RoutingGraphWarning::None;

    switch (command.kind) {
    case AudioGraphCommandKind::AssignDeckOutput: {
        const auto output = decodeOutputBus(command.aux);
        StereoOutputPair pair{};
        const auto validation = outputPairForBus(next.layout, output, pair);
        if (!validation.ok()) {
            return validation;
        }
        if (!next.layout.isAvailable(pair)) {
            return RoutingGraphResult::failure(RoutingGraphError::UnavailableOutputChannel);
        }
        next.decks[deckId.index()].assignment.mainOutput = output;
        next.decks[deckId.index()].assignedOutput = pair;
        warning = validation.warning;
        break;
    }
    case AudioGraphCommandKind::SetCueEnabled:
        next.decks[deckId.index()].assignment.cueEnabled = command.value > 0.0F;
        if (next.decks[deckId.index()].assignment.cueEnabled) {
            warning = next.layout.cueWarning();
        }
        break;
    case AudioGraphCommandKind::InsertPluginSlot:
        if (command.aux >= kPluginSlotsPerDeck) {
            return RoutingGraphResult::failure(RoutingGraphError::InvalidPluginSlot);
        }
        next.decks[deckId.index()].pluginSlots[command.aux].state = command.value > 0.0F ? PluginSlotState::MissingPluginPlaceholder
                                                                                        : PluginSlotState::HostedPluginPlaceholder;
        break;
    case AudioGraphCommandKind::RemovePluginSlot:
        if (command.aux >= kPluginSlotsPerDeck) {
            return RoutingGraphResult::failure(RoutingGraphError::InvalidPluginSlot);
        }
        next.decks[deckId.index()].pluginSlots[command.aux].state = PluginSlotState::EmptyPlaceholder;
        break;
    case AudioGraphCommandKind::SetTransportState:
    case AudioGraphCommandKind::SetDeckGain:
    case AudioGraphCommandKind::ReplaceSnapshot:
        break;
    }

    ++next.revision;
    rebuildSnapshot(next);
    next.warning = mergedWarning(next.warning, warning);
    activeSnapshot_ = next;
    return RoutingGraphResult::success(next.warning);
}

GraphNodeId deckNodeId(core::DeckId deckId) noexcept {
    return GraphNodeId{kDeckNodeBase + static_cast<std::uint32_t>(deckId.index())};
}

GraphNodeId pluginSlotNodeId(core::DeckId deckId, std::size_t slotIndex) noexcept {
    return GraphNodeId{kPluginNodeBase + (static_cast<std::uint32_t>(deckId.index()) * 10U) + static_cast<std::uint32_t>(slotIndex)};
}

GraphNodeId masterBusNodeId() noexcept {
    return GraphNodeId{kMasterBusNode};
}

GraphNodeId cueBusNodeId() noexcept {
    return GraphNodeId{kCueBusNode};
}

GraphNodeId deviceOutputNodeId(std::size_t outputIndex) noexcept {
    return GraphNodeId{kDeviceOutputNodeBase + static_cast<std::uint32_t>(outputIndex)};
}

RoutingGraphResult outputPairForBus(const RoutingDeviceLayout& layout,
                                    core::OutputBus output,
                                    StereoOutputPair& pair) noexcept {
    switch (output) {
    case core::OutputBus::Master:
        pair = layout.masterOutput;
        return RoutingGraphResult::success();
    case core::OutputBus::Cue:
        return RoutingGraphResult::failure(RoutingGraphError::InvalidMainOutput);
    case core::OutputBus::Output1:
    case core::OutputBus::Output2:
    case core::OutputBus::Output3:
    case core::OutputBus::Output4: {
        const auto index = static_cast<std::size_t>(output) - static_cast<std::size_t>(core::OutputBus::Output1);
        pair = layout.deckOutputs[index];
        return RoutingGraphResult::success();
    }
    }
    return RoutingGraphResult::failure(RoutingGraphError::InvalidMainOutput);
}

}
