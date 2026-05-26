#include "decks/AudioDeck.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace djapp::decks {

namespace {

float deterministicSample(std::uint32_t frame, std::uint32_t channel) noexcept {
    const auto phase = static_cast<float>((frame % 32U) + 1U) / 32.0F;
    return channel == 0U ? phase * 0.25F : -phase * 0.125F;
}

float maxAbs(float left, float right) noexcept {
    return std::max(std::abs(left), std::abs(right));
}

}

PreparedAudioMedia PreparedAudioMedia::deterministicTestWav(std::uint32_t frameCount,
                                                           std::uint32_t sampleRateHz) {
    PreparedAudioMedia media;
    media.frameCount = std::min<std::uint32_t>(frameCount, kMaxPreparedAudioFrames);
    media.sampleRateHz = sampleRateHz;
    media.label = "deterministic-test-wav";
    for (std::uint32_t frame = 0; frame < media.frameCount; ++frame) {
        for (std::uint32_t channel = 0; channel < kAudioDeckChannels; ++channel) {
            media.interleavedSamples[(frame * kAudioDeckChannels) + channel] = deterministicSample(frame, channel);
        }
    }
    return media;
}

AudioDeckMediaReference AudioDeckMediaReference::missing() noexcept {
    return AudioDeckMediaReference{AudioDeckMediaKind::Missing, {}};
}

AudioDeckMediaReference AudioDeckMediaReference::corrupt() noexcept {
    return AudioDeckMediaReference{AudioDeckMediaKind::Corrupt, {}};
}

AudioDeckMediaReference AudioDeckMediaReference::unsupported() noexcept {
    return AudioDeckMediaReference{AudioDeckMediaKind::Unsupported, {}};
}

AudioDeckMediaReference AudioDeckMediaReference::deterministicTestWav(PreparedAudioMedia media) {
    return AudioDeckMediaReference{AudioDeckMediaKind::DeterministicTestWav, std::move(media)};
}

AudioFileDeck::AudioFileDeck(core::DeckId id) noexcept {
    state_.id = id;
    state_.type = core::DeckType::AudioFile;
    state_.routing = core::RoutingAssignment{core::OutputBus::Master, false};
}

const AudioDeckState& AudioFileDeck::state() const noexcept {
    return state_;
}

AudioDeckLoadResult AudioFileDeck::loadPreparedMedia(const AudioDeckMediaReference& reference) {
    switch (reference.kind) {
    case AudioDeckMediaKind::Missing:
        return AudioDeckLoadResult::failure(AudioDeckLoadError::MissingMedia);
    case AudioDeckMediaKind::Unsupported:
        return AudioDeckLoadResult::failure(AudioDeckLoadError::UnsupportedMedia);
    case AudioDeckMediaKind::Corrupt:
        return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
    case AudioDeckMediaKind::DeterministicTestWav:
        if (reference.media.frameCount == 0U) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::EmptyMedia);
        }
        if (reference.media.frameCount > kMaxPreparedAudioFrames) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::TooManyFrames);
        }
        media_ = reference.media;
        state_.loaded = true;
        state_.loadedFrameCount = media_.frameCount;
        state_.loadedSampleRateHz = media_.sampleRateHz;
        state_.transport = core::TransportState{};
        playheadFrame_ = 0;
        return AudioDeckLoadResult::success();
    }
    return AudioDeckLoadResult::failure(AudioDeckLoadError::UnsupportedMedia);
}

void AudioFileDeck::cueToStart() noexcept {
    state_.transport.positionBeats = 0.0;
    playheadFrame_ = 0;
}

void AudioFileDeck::play() noexcept {
    if (state_.loaded) {
        state_.transport.playing = true;
    }
}

void AudioFileDeck::stop() noexcept {
    state_.transport.playing = false;
}

AudioDeckRenderResult AudioFileDeck::renderFromPreparedMedia(const audio::AudioRenderConfiguration& configuration,
                                                            std::uint32_t blockCount,
                                                            const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot) noexcept {
    const auto& routedDeck = routingSnapshot.deck(state_.id);
    state_.routing = routedDeck.assignment;

    AudioDeckRenderResult result;
    result.renderedFrames = static_cast<std::uint64_t>(configuration.bufferFrames) * blockCount;
    result.routing = routedDeck.assignment;
    result.assignedOutput = routedDeck.assignedOutput;
    result.cueEnabled = routedDeck.assignment.cueEnabled;

    if (!state_.loaded || !state_.transport.playing || media_.frameCount == 0U) {
        return result;
    }

    for (std::uint64_t frame = 0; frame < result.renderedFrames; ++frame) {
        if (playheadFrame_ >= media_.frameCount) {
            state_.transport.playing = false;
            break;
        }
        const auto sampleIndex = playheadFrame_ * kAudioDeckChannels;
        result.peakMagnitude = std::max(result.peakMagnitude,
                                        maxAbs(media_.interleavedSamples[sampleIndex],
                                               media_.interleavedSamples[sampleIndex + 1U]));
        ++playheadFrame_;
    }

    state_.transport.positionBeats = static_cast<double>(playheadFrame_) / static_cast<double>(media_.sampleRateHz) * 2.0;
    return result;
}

const char* toString(AudioDeckLoadError error) noexcept {
    switch (error) {
    case AudioDeckLoadError::None:
        return "none";
    case AudioDeckLoadError::MissingMedia:
        return "missing-media";
    case AudioDeckLoadError::CorruptMedia:
        return "corrupt-media";
    case AudioDeckLoadError::UnsupportedMedia:
        return "unsupported-media";
    case AudioDeckLoadError::EmptyMedia:
        return "empty-media";
    case AudioDeckLoadError::TooManyFrames:
        return "too-many-frames";
    }
    return "unsupported-media";
}

}
