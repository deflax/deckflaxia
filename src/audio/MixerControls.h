#pragma once

#include "audio/AudioGraphContracts.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/DomainModels.h"
#include "midi/MidiLearn.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace djapp::audio {

constexpr std::size_t kMixerDeckCount = routing::kDeckCount;
constexpr std::size_t kMixerCommandCapacity = 48;

enum class MixerCommandKind : std::uint8_t {
    SetDeckVolume,
    SetDeckGain,
    SetDeckEqLow,
    SetDeckEqMid,
    SetDeckEqHigh,
    SetCrossfader,
    SetCueEnabled,
    SetDeckOutput,
    SetTransportPlay,
    SetTransportCue,
    TogglePluginBypass,
};

enum class MixerCommandError : std::uint8_t {
    None,
    InvalidDeckId,
    InvalidValue,
    InvalidPluginSlot,
    QueueFull,
    UnsupportedMidiCommand,
    RoutingRejected,
};

struct MixerCommand final {
    MixerCommandKind kind{MixerCommandKind::SetDeckVolume};
    std::uint32_t deckIndex{};
    float value{};
    std::uint32_t aux{};
};

struct MixerCommandResult final {
    MixerCommandError error{MixerCommandError::None};
    routing::RoutingGraphError routingError{routing::RoutingGraphError::None};

    [[nodiscard]] constexpr bool ok() const noexcept { return error == MixerCommandError::None; }
    [[nodiscard]] static constexpr MixerCommandResult success() noexcept { return {}; }
    [[nodiscard]] static constexpr MixerCommandResult failure(MixerCommandError error) noexcept { return MixerCommandResult{error, routing::RoutingGraphError::None}; }
    [[nodiscard]] static constexpr MixerCommandResult routingFailure(routing::RoutingGraphError error) noexcept { return MixerCommandResult{MixerCommandError::RoutingRejected, error}; }
};

struct MixerDeckSnapshot final {
    float volume{1.0F};
    float gain{1.0F};
    float eqLow{1.0F};
    float eqMid{1.0F};
    float eqHigh{1.0F};
    bool cueEnabled{};
    core::OutputBus output{core::OutputBus::Master};
    bool playing{};
    bool transportTouched{};
    std::uint64_t cueEpoch{};
    bool pluginBypassed{};
    float meterLeft{};
    float meterRight{};
};

struct MixerSnapshot final {
    std::uint64_t revision{1};
    std::array<MixerDeckSnapshot, kMixerDeckCount> decks{};
    float crossfader{0.5F};
    float masterLevel{1.0F};
    float cueLevel{1.0F};
};

class FixedMixerCommandQueue final : public AudioGraphCommandQueue {
public:
    [[nodiscard]] bool tryPushFromMessageThread(const AudioGraphCommand& command) noexcept override;
    [[nodiscard]] bool tryPopForAudioThread(AudioGraphCommand& command) noexcept override;
    [[nodiscard]] bool tryPushMixerCommand(const MixerCommand& command) noexcept;
    [[nodiscard]] bool tryPopForMixerUpdate(MixerCommand& command) noexcept;
    [[nodiscard]] std::size_t pendingCount() const noexcept;

private:
    std::array<MixerCommand, kMixerCommandCapacity> commands_{};
    std::size_t writeIndex_{};
    std::size_t audioReadIndex_{};
    std::size_t updateReadIndex_{};
};

class MixerController final {
public:
    [[nodiscard]] const MixerSnapshot& activeSnapshot() const noexcept;
    [[nodiscard]] MixerSnapshot captureSnapshotForAudioCallback() const noexcept;
    [[nodiscard]] std::size_t pendingCommandCount() const noexcept;
    [[nodiscard]] FixedMixerCommandQueue& commandQueue() noexcept;

    [[nodiscard]] MixerCommandResult enqueue(MixerCommand command) noexcept;
    [[nodiscard]] MixerCommandResult enqueueFromMidi(const midi::MidiTargetCommand& command) noexcept;
    [[nodiscard]] MixerCommandResult processPendingUpdatesOutsideCallback(routing::AudioRoutingGraphController& routing) noexcept;
    void publishDeckMeter(core::DeckId deckId, float left, float right) noexcept;

private:
    [[nodiscard]] MixerCommandResult validate(MixerCommand command) const noexcept;
    [[nodiscard]] MixerCommandResult apply(MixerCommand command, routing::AudioRoutingGraphController& routing) noexcept;

    FixedMixerCommandQueue commands_{};
    MixerSnapshot activeSnapshot_{};
};

[[nodiscard]] float deckMainGain(const MixerSnapshot& snapshot, core::DeckId deckId) noexcept;
[[nodiscard]] float deckCueGain(const MixerSnapshot& snapshot, core::DeckId deckId) noexcept;
[[nodiscard]] const char* toString(MixerCommandError error) noexcept;

}
