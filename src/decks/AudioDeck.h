#pragma once

#include "audio/AudioEngine.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/DomainModels.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace djapp::decks {

constexpr std::size_t kAudioDeckChannels = 2;
constexpr std::size_t kMaxPreparedAudioFrames = 4096;

enum class AudioDeckMediaKind : std::uint8_t {
    Missing,
    Unsupported,
    Corrupt,
    DeterministicTestWav,
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
    std::array<float, kMaxPreparedAudioFrames * kAudioDeckChannels> interleavedSamples{};
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
};

struct AudioDeckState final {
    core::DeckId id{};
    core::DeckType type{core::DeckType::AudioFile};
    core::TransportState transport{};
    core::RoutingAssignment routing{};
    bool loaded{false};
    std::uint32_t loadedFrameCount{};
    std::uint32_t loadedSampleRateHz{};
};

struct AudioDeckRenderResult final {
    std::uint64_t renderedFrames{};
    float peakMagnitude{};
    core::RoutingAssignment routing{};
    audio::routing::StereoOutputPair assignedOutput{};
    bool cueEnabled{};
};

class AudioFileDeck final {
public:
    explicit AudioFileDeck(core::DeckId id) noexcept;

    [[nodiscard]] const AudioDeckState& state() const noexcept;
    [[nodiscard]] AudioDeckLoadResult loadPreparedMedia(const AudioDeckMediaReference& reference);
    void cueToStart() noexcept;
    void play() noexcept;
    void stop() noexcept;
    [[nodiscard]] AudioDeckRenderResult renderFromPreparedMedia(const audio::AudioRenderConfiguration& configuration,
                                                               std::uint32_t blockCount,
                                                               const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot) noexcept;

private:
    AudioDeckState state_{};
    PreparedAudioMedia media_{};
    std::uint32_t playheadFrame_{};
};

[[nodiscard]] const char* toString(AudioDeckLoadError error) noexcept;

}
