#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace djapp::audio {

enum class AudioGraphCommandKind : std::uint8_t {
    SetTransportState,
    SetDeckGain,
    AssignDeckOutput,
    SetCueEnabled,
    InsertPluginSlot,
    RemovePluginSlot,
    ReplaceSnapshot,
};

struct AudioGraphCommand final {
    AudioGraphCommandKind kind{};
    std::uint32_t targetId{};
    float value{};
    std::uint32_t aux{};
};

struct SampleRateBufferPair final {
    std::uint32_t sampleRateHz{};
    std::uint32_t bufferFrames{};
};

constexpr std::array<SampleRateBufferPair, 6> kAlphaSampleRateBufferMatrix{{
    {44100, 64},
    {44100, 128},
    {44100, 512},
    {48000, 64},
    {48000, 128},
    {48000, 512},
}};

class ImmutableAudioSnapshot final {
public:
    constexpr ImmutableAudioSnapshot(std::uint64_t revision, float masterGain, std::uint8_t activeDeckCount) noexcept
        : revision_(revision), masterGain_(masterGain), activeDeckCount_(activeDeckCount) {}

    [[nodiscard]] constexpr std::uint64_t revision() const noexcept {
        return revision_;
    }

    [[nodiscard]] constexpr float masterGain() const noexcept {
        return masterGain_;
    }

    [[nodiscard]] constexpr std::uint8_t activeDeckCount() const noexcept {
        return activeDeckCount_;
    }

private:
    std::uint64_t revision_{};
    float masterGain_{1.0F};
    std::uint8_t activeDeckCount_{};
};

class AudioGraphCommandQueue {
public:
    AudioGraphCommandQueue() = default;
    AudioGraphCommandQueue(const AudioGraphCommandQueue&) = delete;
    AudioGraphCommandQueue& operator=(const AudioGraphCommandQueue&) = delete;
    virtual ~AudioGraphCommandQueue() = default;

    virtual bool tryPushFromMessageThread(const AudioGraphCommand& command) noexcept = 0;
    virtual bool tryPopForAudioThread(AudioGraphCommand& command) noexcept = 0;
};

class AudioCallbackContract final {
public:
    constexpr AudioCallbackContract(const ImmutableAudioSnapshot& snapshot,
                                    AudioGraphCommandQueue& commands) noexcept
        : snapshot_(&snapshot), commands_(&commands) {}

    [[nodiscard]] constexpr const ImmutableAudioSnapshot& snapshot() const noexcept {
        return *snapshot_;
    }

    [[nodiscard]] constexpr AudioGraphCommandQueue& commandQueue() const noexcept {
        return *commands_;
    }

private:
    const ImmutableAudioSnapshot* snapshot_{};
    AudioGraphCommandQueue* commands_{};
};

template <typename T>
inline constexpr bool isAudioCallbackDependencyAllowed =
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ImmutableAudioSnapshot> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, AudioGraphCommandQueue> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, AudioCallbackContract>;

template <typename... Dependencies>
inline constexpr bool areAudioCallbackDependenciesAllowed =
    (... && isAudioCallbackDependencyAllowed<Dependencies>);

template <typename... Dependencies>
struct AudioCallbackBoundary final {
    static_assert(areAudioCallbackDependenciesAllowed<Dependencies...>,
                  "audio callback dependencies must be limited to immutable snapshots and non-blocking graph commands");
};

static_assert(std::is_trivially_copyable_v<AudioGraphCommand>);
static_assert(std::is_trivially_copyable_v<SampleRateBufferPair>);
static_assert(std::is_trivially_copyable_v<ImmutableAudioSnapshot>);

}
