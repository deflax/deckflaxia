#pragma once

#include "audio/AudioEngine.h"
#include "audio/MixerControls.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "decks/AudioDeck.h"
#include "plugins/PluginChainProcessor.h"

#include <array>
#include <cstdint>
#include <string>

namespace deckflaxia::decks {

constexpr std::uint32_t kFourDeckOutputChannels = 8;
constexpr std::uint32_t kFourDeckMaxRenderFrames = 512;
constexpr double kFourDeckCallbackBudgetMs = 7.47;

enum class FourDeckPlaybackError : std::uint8_t {
    None,
    InvalidDeckId,
    InvalidRenderConfiguration,
    DeckLoadFailed,
};

struct FourDeckLoadResult final {
    FourDeckPlaybackError error{FourDeckPlaybackError::None};
    AudioDeckLoadError deckError{AudioDeckLoadError::None};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == FourDeckPlaybackError::None && deckError == AudioDeckLoadError::None;
    }

    [[nodiscard]] static constexpr FourDeckLoadResult success() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr FourDeckLoadResult failure(FourDeckPlaybackError error,
                                                             AudioDeckLoadError deckError = AudioDeckLoadError::None) noexcept {
        return FourDeckLoadResult{error, deckError};
    }
};

struct FourDeckRenderMetrics final {
    std::uint64_t renderedFrames{};
    std::uint64_t callbackCount{};
    std::uint64_t underrunFrames{};
    std::uint64_t underrunCallbacks{};
    double maxCallbackMs{};
    float peakMagnitude{};
    std::uint32_t maxStretchLatencyFrames{};
};

struct FourDeckRenderResult final {
    FourDeckPlaybackError error{FourDeckPlaybackError::None};
    FourDeckRenderMetrics metrics{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == FourDeckPlaybackError::None;
    }
};

class FourDeckPlaybackCore final {
public:
    FourDeckPlaybackCore() noexcept;

    [[nodiscard]] const AudioFileDeck& deck(core::DeckId id) const noexcept;
    [[nodiscard]] AudioFileDeck& deck(core::DeckId id) noexcept;
    [[nodiscard]] const FourDeckRenderMetrics& metrics() const noexcept;
    [[nodiscard]] const std::array<float, kFourDeckMaxRenderFrames * kFourDeckOutputChannels>& lastInterleavedOutput() const noexcept;
    [[nodiscard]] const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot() const noexcept;
    [[nodiscard]] audio::routing::AudioRoutingGraphController& routing() noexcept;
    [[nodiscard]] const audio::MixerSnapshot& mixerSnapshot() const noexcept;
    [[nodiscard]] audio::MixerController& mixer() noexcept;
    [[nodiscard]] const plugins::OfflinePluginChainHost& deckPluginChain(core::DeckId id) const noexcept;
    [[nodiscard]] const plugins::OfflinePluginChainHost& masterPluginChain() const noexcept;

    [[nodiscard]] FourDeckLoadResult loadDeck(core::DeckId id, const AudioDeckMediaReference& reference);
    [[nodiscard]] plugins::PluginHostResult setDeckPluginChain(core::DeckId id, core::PluginChainDescriptor chain);
    [[nodiscard]] plugins::PluginHostResult setMasterPluginChain(core::PluginChainDescriptor chain);
    [[nodiscard]] plugins::PluginHostResult setDeckPluginParameter(core::DeckId id,
                                                                   std::size_t slotIndex,
                                                                   const std::string& parameterId,
                                                                   double normalizedValue) noexcept;
    [[nodiscard]] plugins::PluginHostResult setMasterPluginParameter(std::size_t slotIndex,
                                                                     const std::string& parameterId,
                                                                     double normalizedValue) noexcept;
    [[nodiscard]] FourDeckPlaybackError play(core::DeckId id) noexcept;
    [[nodiscard]] FourDeckPlaybackError pause(core::DeckId id) noexcept;
    [[nodiscard]] FourDeckPlaybackError stop(core::DeckId id) noexcept;
    [[nodiscard]] FourDeckPlaybackError cue(core::DeckId id) noexcept;
    [[nodiscard]] FourDeckPlaybackError seek(core::DeckId id, std::uint32_t frame) noexcept;
    [[nodiscard]] FourDeckPlaybackError configureTimeStretch(core::DeckId id, const audio::TimeStretchSettings& settings) noexcept;
    [[nodiscard]] FourDeckPlaybackError syncTempo(core::DeckId id, double sourceBpm, double targetBpm, bool pitchLockEnabled) noexcept;
    [[nodiscard]] FourDeckPlaybackError setPitchLock(core::DeckId id, bool enabled, double pitchShiftCents = 0.0) noexcept;
    [[nodiscard]] FourDeckPlaybackError setTimeStretchBypass(core::DeckId id, bool bypass) noexcept;

    [[nodiscard]] FourDeckRenderResult renderNextBlock(const audio::AudioRenderConfiguration& configuration) noexcept;
    [[nodiscard]] FourDeckRenderResult renderOffline(const audio::AudioRenderConfiguration& configuration,
                                                     std::uint32_t blockCount) noexcept;

private:
    [[nodiscard]] bool validDeckId(core::DeckId id) const noexcept;
    [[nodiscard]] bool validRenderConfiguration(const audio::AudioRenderConfiguration& configuration) const noexcept;

    audio::routing::AudioRoutingGraphController routing_;
    audio::MixerController mixer_;
    std::array<AudioFileDeck, audio::routing::kDeckCount> decks_;
    std::array<plugins::OfflinePluginChainHost, audio::routing::kDeckCount> deckPluginChains_;
    plugins::OfflinePluginChainHost masterPluginChain_;
    std::array<std::uint64_t, audio::routing::kDeckCount> processedCueEpochs_{};
    std::array<std::array<float, kFourDeckMaxRenderFrames * kAudioDeckChannels>, audio::routing::kDeckCount> deckPluginBuffers_{};
    std::array<float, kFourDeckMaxRenderFrames * kAudioDeckChannels> masterPluginBuffer_{};
    std::array<float, kFourDeckMaxRenderFrames * kFourDeckOutputChannels> interleavedOutput_{};
    FourDeckRenderMetrics metrics_{};
};

[[nodiscard]] const char* toString(FourDeckPlaybackError error) noexcept;

}
