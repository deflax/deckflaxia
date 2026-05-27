#pragma once

#include "audio/AudioGraphContracts.h"
#include "core/DomainModels.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deckflaxia::audio::routing {

constexpr std::size_t kDeckCount = 4;
constexpr std::size_t kPluginSlotsPerDeck = 4;
constexpr std::size_t kDeviceOutputPairCount = 4;
constexpr std::size_t kMaxGraphNodes = kDeckCount + (kDeckCount * kPluginSlotsPerDeck) + 2 + kDeviceOutputPairCount;
constexpr std::size_t kMaxGraphConnections = (kDeckCount * (kPluginSlotsPerDeck + 2)) + 2;
constexpr std::size_t kRoutingCommandCapacity = 32;

enum class RoutingGraphNodeKind : std::uint8_t {
    Deck,
    PluginSlotPlaceholder,
    CueBus,
    MasterBus,
    DeviceOutputPair,
};

enum class RoutingGraphError : std::uint8_t {
    None,
    InvalidDeckId,
    InvalidPluginSlot,
    InvalidMainOutput,
    UnavailableOutputChannel,
    CommandQueueFull,
};

enum class PluginSlotState : std::uint8_t {
    EmptyPlaceholder,
    HostedPluginPlaceholder,
    MissingPluginPlaceholder,
};

enum class RoutingGraphWarning : std::uint8_t {
    None,
    CueMasterOverlap,
};

struct RoutingGraphResult final {
    RoutingGraphError error{RoutingGraphError::None};
    RoutingGraphWarning warning{RoutingGraphWarning::None};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == RoutingGraphError::None;
    }

    [[nodiscard]] static constexpr RoutingGraphResult success(RoutingGraphWarning warning = RoutingGraphWarning::None) noexcept {
        return RoutingGraphResult{RoutingGraphError::None, warning};
    }

    [[nodiscard]] static constexpr RoutingGraphResult failure(RoutingGraphError error) noexcept {
        return RoutingGraphResult{error, RoutingGraphWarning::None};
    }
};

struct GraphNodeId final {
    std::uint32_t value{};

    friend constexpr bool operator==(GraphNodeId left, GraphNodeId right) noexcept {
        return left.value == right.value;
    }

    friend constexpr bool operator!=(GraphNodeId left, GraphNodeId right) noexcept {
        return !(left == right);
    }
};

struct StereoOutputPair final {
    std::uint32_t left{};
    std::uint32_t right{1};

    [[nodiscard]] constexpr bool fits(std::uint32_t channelCount) const noexcept {
        return left < channelCount && right < channelCount && left != right;
    }

    friend constexpr bool operator==(StereoOutputPair left, StereoOutputPair right) noexcept {
        return left.left == right.left && left.right == right.right;
    }
};

struct RoutingDeviceLayout final {
    std::uint32_t channelCount{2};
    StereoOutputPair masterOutput{0, 1};
    StereoOutputPair cueOutput{0, 1};
    std::array<StereoOutputPair, kDeviceOutputPairCount> deckOutputs{{
        StereoOutputPair{0, 1},
        StereoOutputPair{2, 3},
        StereoOutputPair{4, 5},
        StereoOutputPair{6, 7},
    }};

    [[nodiscard]] static RoutingDeviceLayout forChannelCount(std::uint32_t channelCount) noexcept;
    [[nodiscard]] bool isAvailable(StereoOutputPair pair) const noexcept;
    [[nodiscard]] RoutingGraphWarning cueWarning() const noexcept;
};

struct RoutingGraphNode final {
    GraphNodeId id{};
    RoutingGraphNodeKind kind{RoutingGraphNodeKind::Deck};
    std::uint8_t deckIndex{};
    std::uint8_t slotIndex{};
    StereoOutputPair outputPair{};
};

struct RoutingGraphConnection final {
    GraphNodeId source{};
    GraphNodeId destination{};
};

struct SequencerMidiRoute final {
    GraphNodeId destination{};
    std::size_t eventCount{};
    bool routed{};
};

struct DeckPluginSlotNode final {
    GraphNodeId id{};
    std::uint8_t slotIndex{};
    bool placeholder{true};
    PluginSlotState state{PluginSlotState::EmptyPlaceholder};
};

struct DeckRoutingNode final {
    core::DeckId deckId{};
    GraphNodeId deckNodeId{};
    std::array<DeckPluginSlotNode, kPluginSlotsPerDeck> pluginSlots{};
    core::RoutingAssignment assignment{};
    StereoOutputPair assignedOutput{};
};

struct AudioRoutingGraphSnapshot final {
    std::uint64_t revision{1};
    RoutingDeviceLayout layout{};
    std::array<DeckRoutingNode, kDeckCount> decks{};
    GraphNodeId cueBusNode{};
    GraphNodeId masterBusNode{};
    std::array<GraphNodeId, kDeviceOutputPairCount> deviceOutputNodes{};
    std::array<RoutingGraphNode, kMaxGraphNodes> nodes{};
    std::size_t nodeCount{};
    std::array<RoutingGraphConnection, kMaxGraphConnections> connections{};
    std::size_t connectionCount{};
    RoutingGraphWarning warning{RoutingGraphWarning::None};

    [[nodiscard]] static AudioRoutingGraphSnapshot createDefault(RoutingDeviceLayout layout) noexcept;
    [[nodiscard]] const DeckRoutingNode& deck(core::DeckId id) const noexcept;
    [[nodiscard]] bool hasConnection(GraphNodeId source, GraphNodeId destination) const noexcept;
    [[nodiscard]] bool resolvePluginSlotNode(core::DeckId deckId, std::size_t slotIndex, GraphNodeId& nodeId) const noexcept;
    [[nodiscard]] bool acceptsSequencerMidi(core::DeckId deckId, std::size_t slotIndex) const noexcept;
    [[nodiscard]] SequencerMidiRoute routeSequencerMidi(core::DeckId deckId, std::size_t slotIndex, std::size_t eventCount) const noexcept;
};

struct RoutingRenderResult final {
    std::uint64_t snapshotRevision{};
    std::size_t nodeCount{};
    std::size_t connectionCount{};
    std::size_t pendingCommandCount{};
};

class FixedRoutingCommandQueue final : public AudioGraphCommandQueue {
public:
    [[nodiscard]] bool tryPushFromMessageThread(const AudioGraphCommand& command) noexcept override;
    [[nodiscard]] bool tryPopForAudioThread(AudioGraphCommand& command) noexcept override;
    [[nodiscard]] bool tryPopForGraphUpdate(AudioGraphCommand& command) noexcept;
    [[nodiscard]] std::size_t pendingCount() const noexcept;

private:
    std::array<AudioGraphCommand, kRoutingCommandCapacity> commands_{};
    std::size_t writeIndex_{};
    std::size_t audioReadIndex_{};
    std::size_t updateReadIndex_{};
};

class AudioRoutingGraphController final {
public:
    explicit AudioRoutingGraphController(RoutingDeviceLayout layout = RoutingDeviceLayout::forChannelCount(2)) noexcept;

    [[nodiscard]] const AudioRoutingGraphSnapshot& activeSnapshot() const noexcept;
    [[nodiscard]] AudioRoutingGraphSnapshot captureSnapshotForAudioCallback() const noexcept;
    [[nodiscard]] std::size_t pendingCommandCount() const noexcept;
    [[nodiscard]] FixedRoutingCommandQueue& commandQueue() noexcept;

    [[nodiscard]] RoutingGraphResult enqueueAssignDeckOutput(core::DeckId deckId, core::OutputBus output) noexcept;
    [[nodiscard]] RoutingGraphResult enqueueSetCueEnabled(core::DeckId deckId, bool enabled) noexcept;
    [[nodiscard]] RoutingGraphResult enqueueInsertPluginSlot(core::DeckId deckId, std::size_t slotIndex, bool missingPlugin) noexcept;
    [[nodiscard]] RoutingGraphResult enqueueRemovePluginSlot(core::DeckId deckId, std::size_t slotIndex) noexcept;
    [[nodiscard]] RoutingGraphResult processPendingUpdatesOutsideCallback() noexcept;
    [[nodiscard]] RoutingRenderResult renderFromAudioCallback(const AudioRoutingGraphSnapshot& snapshot) const noexcept;

private:
    [[nodiscard]] RoutingGraphResult validateDeckOutput(core::DeckId deckId, core::OutputBus output) const noexcept;
    [[nodiscard]] RoutingGraphResult validatePluginSlot(core::DeckId deckId, std::size_t slotIndex) const noexcept;
    [[nodiscard]] RoutingGraphResult applyCommand(AudioGraphCommand command) noexcept;

    FixedRoutingCommandQueue commands_{};
    AudioRoutingGraphSnapshot activeSnapshot_{};
};

[[nodiscard]] GraphNodeId deckNodeId(core::DeckId deckId) noexcept;
[[nodiscard]] GraphNodeId pluginSlotNodeId(core::DeckId deckId, std::size_t slotIndex) noexcept;
[[nodiscard]] GraphNodeId masterBusNodeId() noexcept;
[[nodiscard]] GraphNodeId cueBusNodeId() noexcept;
[[nodiscard]] GraphNodeId deviceOutputNodeId(std::size_t outputIndex) noexcept;
[[nodiscard]] RoutingGraphResult outputPairForBus(const RoutingDeviceLayout& layout,
                                                  core::OutputBus output,
                                                  StereoOutputPair& pair) noexcept;

}
