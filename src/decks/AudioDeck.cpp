#include "decks/AudioDeck.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <utility>

namespace djapp::decks {

namespace {

float deterministicSample(std::uint32_t frame, std::uint32_t channel) noexcept {
    const auto phase = static_cast<float>((frame % 32U) + 1U) / 32.0F;
    return channel == 0U ? phase * 0.25F : -phase * 0.125F;
}

audio::TimeStretchStatus initialTimeStretchStatus(const audio::TimeStretchSettings& settings) noexcept {
    audio::TimeStretchStatus status;
    status.engine = audio::primaryTimeStretchEngineKind();
    status.mode = audio::TimeStretchProcessMode::LiveRealTime;
    status.available = true;
    status.bypassed = settings.bypass;
    status.fallback = status.engine != audio::TimeStretchEngineKind::RubberBandRealTime;
    status.playbackRate = audio::playbackRateForSettings(settings);
    status.effectiveTempoBpm = audio::effectiveTempoForSettings(settings);
    status.pitchDriftCents = audio::pitchDriftCentsForSettings(settings);
    status.latencyFrames = 0;
    status.preferredStartPadFrames = 0;
    status.startDelayFrames = 0;
    status.workerBufferFrames = 1024;
    return status;
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
    media.interleavedSamples.resize(static_cast<std::size_t>(media.frameCount) * kAudioDeckChannels);
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

AudioDeckMediaReference AudioDeckMediaReference::preparedAudio(PreparedAudioMedia media) {
    return AudioDeckMediaReference{AudioDeckMediaKind::PreparedAudio, std::move(media)};
}

AudioFileDeck::AudioFileDeck(core::DeckId id) noexcept {
    state_.id = id;
    state_.type = core::DeckType::AudioFile;
    state_.routing = core::RoutingAssignment{core::OutputBus::Master, false};
    state_.timeStretchStatus = initialTimeStretchStatus(state_.timeStretch);
    state_.timeStretchStatus.fallbackEvents = timeStretchFallbackEvents_;
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
    case AudioDeckMediaKind::PreparedAudio:
        if (reference.media.frameCount == 0U) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::EmptyMedia);
        }
        if (reference.media.frameCount > kMaxPreparedAudioFrames) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::TooManyFrames);
        }
        if (reference.media.interleavedSamples.size() < static_cast<std::size_t>(reference.media.frameCount) * kAudioDeckChannels) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
        }
        media_ = reference.media;
        state_.loaded = true;
        state_.loadedFrameCount = media_.frameCount;
        state_.loadedSampleRateHz = media_.sampleRateHz;
        state_.transport = core::TransportState{};
        playheadFrame_ = 0;
        playheadFrameExact_ = 0.0;
        state_.timeStretchStatus = initialTimeStretchStatus(state_.timeStretch);
        return AudioDeckLoadResult::success();
    }
    return AudioDeckLoadResult::failure(AudioDeckLoadError::UnsupportedMedia);
}

void AudioFileDeck::cueToStart() noexcept {
    seekToFrame(0);
}

void AudioFileDeck::seekToFrame(std::uint32_t frame) noexcept {
    if (!state_.loaded || media_.frameCount == 0U) {
        playheadFrame_ = 0;
        playheadFrameExact_ = 0.0;
        state_.transport.positionBeats = 0.0;
        return;
    }
    playheadFrame_ = std::min(frame, media_.frameCount - 1U);
    playheadFrameExact_ = static_cast<double>(playheadFrame_);
    state_.transport.positionBeats = static_cast<double>(playheadFrame_) / static_cast<double>(media_.sampleRateHz) * 2.0;
}

void AudioFileDeck::play() noexcept {
    if (state_.loaded) {
        state_.transport.playing = true;
    }
}

void AudioFileDeck::stop() noexcept {
    state_.transport.playing = false;
    cueToStart();
}

void AudioFileDeck::pause() noexcept {
    state_.transport.playing = false;
}

std::uint32_t AudioFileDeck::playheadFrame() const noexcept {
    return playheadFrame_;
}

double AudioFileDeck::fractionalPlayheadFrame() const noexcept {
    return playheadFrameExact_;
}

const audio::TimeStretchSettings& AudioFileDeck::timeStretchSettings() const noexcept {
    return state_.timeStretch;
}

const audio::TimeStretchStatus& AudioFileDeck::timeStretchStatus() const noexcept {
    return state_.timeStretchStatus;
}

void AudioFileDeck::configureTimeStretch(const audio::TimeStretchSettings& settings) noexcept {
    state_.timeStretch = settings;
    state_.timeStretchStatus = initialTimeStretchStatus(state_.timeStretch);
    state_.timeStretchStatus.fallbackEvents = timeStretchFallbackEvents_;
}

void AudioFileDeck::syncTempoTo(double sourceBpm, double targetBpm, bool pitchLockEnabled) noexcept {
    audio::TimeStretchSettings settings = state_.timeStretch;
    settings.sourceBpm = sourceBpm;
    settings.targetBpm = targetBpm;
    settings.tempoSyncEnabled = true;
    settings.pitchLockEnabled = pitchLockEnabled;
    configureTimeStretch(settings);
}

void AudioFileDeck::setPitchLock(bool enabled, double pitchShiftCents) noexcept {
    audio::TimeStretchSettings settings = state_.timeStretch;
    settings.pitchLockEnabled = enabled;
    settings.pitchShiftCents = pitchShiftCents;
    configureTimeStretch(settings);
}

void AudioFileDeck::setTimeStretchBypass(bool bypass) noexcept {
    audio::TimeStretchSettings settings = state_.timeStretch;
    settings.bypass = bypass;
    configureTimeStretch(settings);
}

void AudioFileDeck::noteTimeStretchFallbackEvent() noexcept {
    ++timeStretchFallbackEvents_;
    state_.timeStretchStatus.fallbackEvents = timeStretchFallbackEvents_;
}

AudioDeckRenderResult AudioFileDeck::renderFromPreparedMedia(const audio::AudioRenderConfiguration& configuration,
                                                             std::uint32_t blockCount,
                                                             const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot) noexcept {
    AudioDeckRenderResult aggregate;
    for (std::uint32_t block = 0; block < blockCount; ++block) {
        const auto blockResult = renderNextBlockToInterleavedOutput(configuration, routingSnapshot, nullptr, 0);
        aggregate.renderedFrames += blockResult.renderedFrames;
        aggregate.peakMagnitude = std::max(aggregate.peakMagnitude, blockResult.peakMagnitude);
        aggregate.routing = blockResult.routing;
        aggregate.assignedOutput = blockResult.assignedOutput;
        aggregate.cueEnabled = blockResult.cueEnabled;
        aggregate.underrunFrames += blockResult.underrunFrames;
    }
    return aggregate;
}

AudioDeckRenderResult AudioFileDeck::renderNextBlockToInterleavedOutput(const audio::AudioRenderConfiguration& configuration,
                                                                        const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot,
                                                                        float* interleavedOutput,
                                                                        std::uint32_t outputChannels,
                                                                        float mainGain,
                                                                        float cueGain) noexcept {
    std::array<float, kAudioDeckChannels * 512U> stereoScratch{};
    const auto scratchFrames = std::min<std::uint32_t>(configuration.bufferFrames, 512U);
    std::fill_n(stereoScratch.data(), static_cast<std::size_t>(scratchFrames) * kAudioDeckChannels, 0.0F);
    auto result = renderNextBlockToStereoBuffer(configuration, routingSnapshot, stereoScratch.data());
    const auto& routedDeck = routingSnapshot.deck(state_.id);
    if (interleavedOutput == nullptr) {
        return result;
    }

    const auto mainOutput = routedDeck.assignedOutput;
    const auto cueOutput = routingSnapshot.layout.cueOutput;
    const auto canMixMain = interleavedOutput != nullptr && mainOutput.fits(outputChannels);
    const auto canMixCue = interleavedOutput != nullptr && result.cueEnabled && cueOutput.fits(outputChannels);
    for (std::uint64_t frame = 0; frame < result.renderedFrames && frame < scratchFrames; ++frame) {
        const auto sourceIndex = static_cast<std::size_t>(frame) * kAudioDeckChannels;
        if (canMixMain) {
            const auto outputIndex = static_cast<std::size_t>(frame) * outputChannels;
            interleavedOutput[outputIndex + mainOutput.left] += stereoScratch[sourceIndex] * mainGain;
            interleavedOutput[outputIndex + mainOutput.right] += stereoScratch[sourceIndex + 1U] * mainGain;
        }
        if (canMixCue) {
            const auto outputIndex = static_cast<std::size_t>(frame) * outputChannels;
            interleavedOutput[outputIndex + cueOutput.left] += stereoScratch[sourceIndex] * cueGain;
            interleavedOutput[outputIndex + cueOutput.right] += stereoScratch[sourceIndex + 1U] * cueGain;
        }
    }
    return result;
}

AudioDeckRenderResult AudioFileDeck::renderNextBlockToStereoBuffer(const audio::AudioRenderConfiguration& configuration,
                                                                   const audio::routing::AudioRoutingGraphSnapshot& routingSnapshot,
                                                                   float* interleavedStereo) noexcept {
    const auto& routedDeck = routingSnapshot.deck(state_.id);
    state_.routing = routedDeck.assignment;

    AudioDeckRenderResult result;
    result.renderedFrames = configuration.bufferFrames;
    result.routing = routedDeck.assignment;
    result.assignedOutput = routedDeck.assignedOutput;
    result.cueEnabled = routedDeck.assignment.cueEnabled;
    result.timeStretch = state_.timeStretchStatus;

    if (!state_.loaded || !state_.transport.playing || media_.frameCount == 0U) {
        return result;
    }

    for (std::uint64_t frame = 0; frame < result.renderedFrames; ++frame) {
        if (playheadFrame_ >= media_.frameCount) {
            state_.transport.playing = false;
            result.underrunFrames = result.renderedFrames - frame;
            break;
        }
        const auto sourceFrame = static_cast<std::uint32_t>(playheadFrameExact_);
        if (sourceFrame >= media_.frameCount) {
            state_.transport.playing = false;
            result.underrunFrames = result.renderedFrames - frame;
            break;
        }
        const auto sampleIndex = sourceFrame * kAudioDeckChannels;
        const auto left = media_.interleavedSamples[sampleIndex];
        const auto right = media_.interleavedSamples[sampleIndex + 1U];
        result.peakMagnitude = std::max(result.peakMagnitude,
                                        maxAbs(left, right));
        if (interleavedStereo != nullptr) {
            const auto outputIndex = static_cast<std::size_t>(frame) * kAudioDeckChannels;
            interleavedStereo[outputIndex] += left;
            interleavedStereo[outputIndex + 1U] += right;
        }
        playheadFrameExact_ += audio::playbackRateForSettings(state_.timeStretch);
        playheadFrame_ = static_cast<std::uint32_t>(std::min<double>(playheadFrameExact_, static_cast<double>(media_.frameCount)));
    }

    state_.timeStretchStatus = initialTimeStretchStatus(state_.timeStretch);
    state_.timeStretchStatus.inputFramesConsumed = static_cast<std::uint64_t>(playheadFrameExact_);
    state_.timeStretchStatus.underrunFrames += result.underrunFrames;
    state_.timeStretchStatus.fallbackEvents = timeStretchFallbackEvents_;
    result.timeStretch = state_.timeStretchStatus;
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

namespace {

std::uint16_t readU16(const std::vector<unsigned char>& bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t readU32(const std::vector<unsigned char>& bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

bool chunkIdEquals(const std::vector<unsigned char>& bytes, std::size_t offset, const char* chunkId) noexcept {
    return bytes[offset] == static_cast<unsigned char>(chunkId[0]) &&
           bytes[offset + 1U] == static_cast<unsigned char>(chunkId[1]) &&
           bytes[offset + 2U] == static_cast<unsigned char>(chunkId[2]) &&
           bytes[offset + 3U] == static_cast<unsigned char>(chunkId[3]);
}

}

AudioDeckLoadResult loadPcm16WavFileToPreparedMedia(const std::filesystem::path& path,
                                                    PreparedAudioMedia& media) {
    if (!std::filesystem::exists(path)) {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::MissingMedia);
    }
    if (path.extension() != ".wav") {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::UnsupportedMedia);
    }

    std::ifstream input(path, std::ios::binary);
    const std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    if (bytes.size() < 44U || !chunkIdEquals(bytes, 0, "RIFF") || !chunkIdEquals(bytes, 8, "WAVE")) {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
    }

    std::size_t offset = 12;
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::size_t dataOffset = 0;
    std::uint32_t dataSize = 0;

    while (offset + 8U <= bytes.size()) {
        const auto chunkSize = readU32(bytes, offset + 4U);
        const auto payloadOffset = offset + 8U;
        if (payloadOffset + chunkSize > bytes.size()) {
            return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
        }
        if (chunkIdEquals(bytes, offset, "fmt ")) {
            if (chunkSize < 16U) {
                return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
            }
            audioFormat = readU16(bytes, payloadOffset);
            channels = readU16(bytes, payloadOffset + 2U);
            sampleRate = readU32(bytes, payloadOffset + 4U);
            bitsPerSample = readU16(bytes, payloadOffset + 14U);
        } else if (chunkIdEquals(bytes, offset, "data")) {
            dataOffset = payloadOffset;
            dataSize = chunkSize;
        }
        offset = payloadOffset + chunkSize + (chunkSize % 2U);
    }

    if (audioFormat != 1U || channels == 0U || sampleRate == 0U || bitsPerSample != 16U || dataOffset == 0U || dataSize == 0U) {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::CorruptMedia);
    }

    const auto bytesPerFrame = static_cast<std::uint32_t>(channels) * 2U;
    const auto sourceFrames = dataSize / bytesPerFrame;
    if (sourceFrames == 0U) {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::EmptyMedia);
    }
    if (sourceFrames > kMaxPreparedAudioFrames) {
        return AudioDeckLoadResult::failure(AudioDeckLoadError::TooManyFrames);
    }

    PreparedAudioMedia loaded;
    loaded.frameCount = sourceFrames;
    loaded.sampleRateHz = sampleRate;
    loaded.label = path.filename().string();
    loaded.interleavedSamples.resize(static_cast<std::size_t>(loaded.frameCount) * kAudioDeckChannels);
    for (std::uint32_t frame = 0; frame < loaded.frameCount; ++frame) {
        const auto sourceIndex = dataOffset + (static_cast<std::size_t>(frame) * bytesPerFrame);
        const auto leftRaw = static_cast<std::int16_t>(readU16(bytes, sourceIndex));
        const auto rightRaw = channels > 1U ? static_cast<std::int16_t>(readU16(bytes, sourceIndex + 2U)) : leftRaw;
        loaded.interleavedSamples[(static_cast<std::size_t>(frame) * kAudioDeckChannels)] = static_cast<float>(leftRaw) / 32768.0F;
        loaded.interleavedSamples[(static_cast<std::size_t>(frame) * kAudioDeckChannels) + 1U] = static_cast<float>(rightRaw) / 32768.0F;
    }

    media = std::move(loaded);
    return AudioDeckLoadResult::success();
}

}
