#pragma once

#include "audio/AudioEngine.h"
#include "audio/TimeStretchEngine.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/DomainModels.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace deckflaxia::decks {

constexpr std::size_t kAudioDeckChannels = 2;
constexpr std::size_t kMaxPreparedAudioFrames = 600000;

enum class AudioDeckMediaKind : std::uint8_t {
    Missing,
    Unsupported,
    Corrupt,
    DeterministicTestWav,
    PreparedAudio,
};

enum class AudioDeckLoadError : std::uint8_t {
    None,
    MissingMedia,
    CorruptMedia,
    UnsupportedMedia,
    EmptyMedia,
    TooManyFrames,
};

struct AudioDeckLoadResult final {
    AudioDeckLoadError error{AudioDeckLoadError::None};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == AudioDeckLoadError::None;
    }

    [[nodiscard]] static constexpr AudioDeckLoadResult success() noexcept {
        return AudioDeckLoadResult{AudioDeckLoadError::None};
    }

    [[nodiscard]] static constexpr AudioDeckLoadResult failure(AudioDeckLoadError error) noexcept {
        return AudioDeckLoadResult{error};
    }
};

struct PreparedAudioMedia final {
    std::vector<float> interleavedSamples{};
    std::uint32_t frameCount{};
    std::uint32_t sampleRateHz{44100};
    std::string label;

    [[nodiscard]] static PreparedAudioMedia deterministicTestWav(std::uint32_t frameCount = 512,
                                                                 std::uint32_t sampleRateHz = 44100);
};

struct AudioDeckMediaReference final {
    AudioDeckMediaKind kind{AudioDeckMediaKind::Missing};
    PreparedAudioMedia media{};

    [[nodiscard]] static AudioDeckMediaReference missing() noexcept;
    [[nodiscard]] static AudioDeckMediaReference corrupt() noexcept;
    [[nodiscard]] static AudioDeckMediaReference unsupported() noexcept;
    [[nodiscard]] static AudioDeckMediaReference deterministicTestWav(PreparedAudioMedia media);
    [[nodiscard]] static AudioDeckMediaReference preparedAudio(PreparedAudioMedia media);
};

struct AudioDeckState final {
    core::DeckId id{};
    core::DeckType type{core::DeckType::AudioFile};
    core::TransportState transport{};
    core::RoutingAssignment routing{};
    bool loaded{false};
    std::uint32_t loadedFrameCount{};
    std::uint32_t loadedSampleRateHz{};
    audio::TimeStretchSettings timeStretch{};
    audio::TimeStretchStatus timeStretchStatus{};
};

struct AudioDeckRenderResult final {
    std::uint64_t renderedFrames{};
    float peakMagnitude{};
    core::RoutingAssignment routing{};
    audio::routing::StereoOutputPair assignedOutput{};
    bool cueEnabled{};
    std::uint64_t underrunFrames{};
    audio::TimeStretchStatus timeStretch{};
};

class AudioFileDeck final {
public:
    explicit AudioFileDeck(core::DeckId id) noexcept;

    [[nodiscard]] const AudioDeckState& state() const noexcept;
    [[nodiscard]] AudioDeckLoadResult loadPreparedMedia(const AudioDeckMediaReference& reference);
    void cueToStart() noexcept;
    void seekToFrame(std::uint32_t frame) noexcept;
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    [[nodiscard]] std::uint32_t playheadFrame() const noexcept;
    [[nodiscard]] double fractionalPlayheadFrame() const noexcept;
    [[nodiscard]] const audio::TimeStretchSettings& timeStretchSettings() const noexcept;
    [[nodiscard]] const audio::TimeStretchStatus& timeStretchStatus() const noexcept;
    void configureTimeStretch(const audio::TimeStretchSettings& settings) noexcept;
    void syncTempoTo(double sourceBpm, double targetBpm, bool pitchLockEnabled) noexcept;
    void setPitchLock(bool enabled, double pitchShiftCents = 0.0) noexcept;
    void setTimeStretchBypass(bool bypass) noexcept;
    void noteTimeStretchFallbackEvent() noexcept;
    [[nodiscard]] AudioDeckRenderResult renderFromPreparedMedia(const audio::AudioRenderConfiguration& configuration,
                                                                std::uint32_t blockCount,
                                                                const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot) noexcept;
    [[nodiscard]] AudioDeckRenderResult renderNextBlockToStereoBuffer(const audio::AudioRenderConfiguration& configuration,
                                                                      const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot,
                                                                      float* interleavedStereo) noexcept;
    [[nodiscard]] AudioDeckRenderResult renderNextBlockToInterleavedOutput(const audio::AudioRenderConfiguration& configuration,
                                                                             const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot,
                                                                             float* interleavedOutput,
                                                                             std::uint32_t outputChannels,
                                                                            float mainGain = 1.0F,
                                                                            float cueGain = 1.0F) noexcept;

private:
    AudioDeckState state_{};
    PreparedAudioMedia media_{};
    std::uint32_t playheadFrame_{};
    double playheadFrameExact_{};
    std::uint64_t timeStretchFallbackEvents_{};
};

[[nodiscard]] const char* toString(AudioDeckLoadError error) noexcept;

[[nodiscard]] AudioDeckLoadResult loadPcm16WavFileToPreparedMedia(const std::filesystem::path& path,
                                                                  PreparedAudioMedia& media);

}
