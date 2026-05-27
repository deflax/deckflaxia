#include "decks/FourDeckPlaybackCore.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace djapp::decks {

namespace {

void mixStereoToOutput(const float* stereo,
                       std::uint32_t frames,
                       audio::routing::StereoOutputPair outputPair,
                       float* interleavedOutput,
                       std::uint32_t outputChannels,
                       float gain) noexcept {
    if (stereo == nullptr || interleavedOutput == nullptr || !outputPair.fits(outputChannels)) {
        return;
    }
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto inputIndex = static_cast<std::size_t>(frame) * kAudioDeckChannels;
        const auto outputIndex = static_cast<std::size_t>(frame) * outputChannels;
        interleavedOutput[outputIndex + outputPair.left] += stereo[inputIndex] * gain;
        interleavedOutput[outputIndex + outputPair.right] += stereo[inputIndex + 1U] * gain;
    }
}

void addStereo(float* destination, const float* source, std::uint32_t frames, float gain) noexcept {
    if (destination == nullptr || source == nullptr) {
        return;
    }
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * kAudioDeckChannels;
        destination[index] += source[index] * gain;
        destination[index + 1U] += source[index + 1U] * gain;
    }
}

}

FourDeckPlaybackCore::FourDeckPlaybackCore() noexcept
    : routing_(audio::routing::RoutingDeviceLayout::forChannelCount(kFourDeckOutputChannels)),
      decks_{AudioFileDeck(core::DeckId::fromIndex(0).value),
             AudioFileDeck(core::DeckId::fromIndex(1).value),
             AudioFileDeck(core::DeckId::fromIndex(2).value),
             AudioFileDeck(core::DeckId::fromIndex(3).value)} {}

const AudioFileDeck& FourDeckPlaybackCore::deck(core::DeckId id) const noexcept {
    return decks_[id.index()];
}

AudioFileDeck& FourDeckPlaybackCore::deck(core::DeckId id) noexcept {
    return decks_[id.index()];
}

const FourDeckRenderMetrics& FourDeckPlaybackCore::metrics() const noexcept {
    return metrics_;
}

const std::array<float, kFourDeckMaxRenderFrames * kFourDeckOutputChannels>& FourDeckPlaybackCore::lastInterleavedOutput() const noexcept {
    return interleavedOutput_;
}

const audio::routing::AudioRoutingGraphSnapshot& FourDeckPlaybackCore::routingSnapshot() const noexcept {
    return routing_.activeSnapshot();
}

audio::routing::AudioRoutingGraphController& FourDeckPlaybackCore::routing() noexcept {
    return routing_;
}

const audio::MixerSnapshot& FourDeckPlaybackCore::mixerSnapshot() const noexcept {
    return mixer_.activeSnapshot();
}

audio::MixerController& FourDeckPlaybackCore::mixer() noexcept {
    return mixer_;
}

const plugins::OfflinePluginChainHost& FourDeckPlaybackCore::deckPluginChain(core::DeckId id) const noexcept {
    return deckPluginChains_[id.index()];
}

const plugins::OfflinePluginChainHost& FourDeckPlaybackCore::masterPluginChain() const noexcept {
    return masterPluginChain_;
}

FourDeckLoadResult FourDeckPlaybackCore::loadDeck(core::DeckId id, const AudioDeckMediaReference& reference) {
    if (!validDeckId(id)) {
        return FourDeckLoadResult::failure(FourDeckPlaybackError::InvalidDeckId);
    }
    const auto result = deck(id).loadPreparedMedia(reference);
    if (!result.ok()) {
        return FourDeckLoadResult::failure(FourDeckPlaybackError::DeckLoadFailed, result.error);
    }
    return FourDeckLoadResult::success();
}

plugins::PluginHostResult FourDeckPlaybackCore::setDeckPluginChain(core::DeckId id, core::PluginChainDescriptor chain) {
    if (!validDeckId(id)) {
        return plugins::PluginHostResult::failure(plugins::PluginHostError::InvalidSlot);
    }
    return deckPluginChains_[id.index()].configure(plugins::PluginChainTargetKind::Deck, std::move(chain), 48000.0, kFourDeckMaxRenderFrames);
}

plugins::PluginHostResult FourDeckPlaybackCore::setMasterPluginChain(core::PluginChainDescriptor chain) {
    return masterPluginChain_.configure(plugins::PluginChainTargetKind::Master, std::move(chain), 48000.0, kFourDeckMaxRenderFrames);
}

plugins::PluginHostResult FourDeckPlaybackCore::setDeckPluginParameter(core::DeckId id,
                                                                       std::size_t slotIndex,
                                                                       const std::string& parameterId,
                                                                       double normalizedValue) noexcept {
    if (!validDeckId(id)) {
        return plugins::PluginHostResult::failure(plugins::PluginHostError::InvalidSlot);
    }
    return deckPluginChains_[id.index()].setParameter(slotIndex, parameterId, normalizedValue);
}

plugins::PluginHostResult FourDeckPlaybackCore::setMasterPluginParameter(std::size_t slotIndex,
                                                                         const std::string& parameterId,
                                                                         double normalizedValue) noexcept {
    return masterPluginChain_.setParameter(slotIndex, parameterId, normalizedValue);
}

FourDeckPlaybackError FourDeckPlaybackCore::play(core::DeckId id) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).play();
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::pause(core::DeckId id) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).pause();
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::stop(core::DeckId id) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).stop();
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::cue(core::DeckId id) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).cueToStart();
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::seek(core::DeckId id, std::uint32_t frame) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).seekToFrame(frame);
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::configureTimeStretch(core::DeckId id, const audio::TimeStretchSettings& settings) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).configureTimeStretch(settings);
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::syncTempo(core::DeckId id, double sourceBpm, double targetBpm, bool pitchLockEnabled) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).syncTempoTo(sourceBpm, targetBpm, pitchLockEnabled);
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::setPitchLock(core::DeckId id, bool enabled, double pitchShiftCents) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).setPitchLock(enabled, pitchShiftCents);
    return FourDeckPlaybackError::None;
}

FourDeckPlaybackError FourDeckPlaybackCore::setTimeStretchBypass(core::DeckId id, bool bypass) noexcept {
    if (!validDeckId(id)) {
        return FourDeckPlaybackError::InvalidDeckId;
    }
    deck(id).setTimeStretchBypass(bypass);
    if (bypass) {
        deck(id).noteTimeStretchFallbackEvent();
    }
    return FourDeckPlaybackError::None;
}

FourDeckRenderResult FourDeckPlaybackCore::renderNextBlock(const audio::AudioRenderConfiguration& configuration) noexcept {
    if (!validRenderConfiguration(configuration)) {
        return FourDeckRenderResult{FourDeckPlaybackError::InvalidRenderConfiguration, metrics_};
    }

    const auto callbackStart = std::chrono::steady_clock::now();
    const auto sampleCount = static_cast<std::size_t>(configuration.bufferFrames) * kFourDeckOutputChannels;
    std::fill_n(interleavedOutput_.data(), sampleCount, 0.0F);
    std::fill_n(masterPluginBuffer_.data(), static_cast<std::size_t>(configuration.bufferFrames) * kAudioDeckChannels, 0.0F);

    (void)mixer_.processPendingUpdatesOutsideCallback(routing_);
    (void)routing_.processPendingUpdatesOutsideCallback();
    const auto snapshot = routing_.captureSnapshotForAudioCallback();
    const auto mixerSnapshot = mixer_.captureSnapshotForAudioCallback();
    for (const auto deckId : core::allDeckIds()) {
        const auto& mixerDeck = mixerSnapshot.decks[deckId.index()];
        if (mixerDeck.cueEpoch != processedCueEpochs_[deckId.index()]) {
            (void)cue(deckId);
            processedCueEpochs_[deckId.index()] = mixerDeck.cueEpoch;
        }
        if (mixerDeck.transportTouched) {
            if (mixerDeck.playing && !deck(deckId).state().transport.playing) {
                (void)play(deckId);
            } else if (!mixerDeck.playing && deck(deckId).state().transport.playing) {
                (void)pause(deckId);
            }
        }
    }
    std::uint64_t blockUnderruns = 0;
    float blockPeak = 0.0F;
    for (auto& audioDeck : decks_) {
        const auto deckId = audioDeck.state().id;
        auto& deckBuffer = deckPluginBuffers_[deckId.index()];
        std::fill_n(deckBuffer.data(), static_cast<std::size_t>(configuration.bufferFrames) * kAudioDeckChannels, 0.0F);
        const auto deckResult = audioDeck.renderNextBlockToStereoBuffer(configuration, snapshot, deckBuffer.data());
        const auto& routedDeck = snapshot.deck(deckId);
        const auto mainGain = audio::deckMainGain(mixerSnapshot, deckId);
        const auto cueGain = audio::deckCueGain(mixerSnapshot, deckId);
        const auto pluginMetrics = deckPluginChains_[deckId.index()].processReplacing(deckBuffer.data(), configuration.bufferFrames, mixerSnapshot.decks[deckId.index()].pluginBypassed);
        if (routedDeck.assignment.mainOutput == core::OutputBus::Master) {
            addStereo(masterPluginBuffer_.data(), deckBuffer.data(), configuration.bufferFrames, mainGain);
        } else {
            mixStereoToOutput(deckBuffer.data(), configuration.bufferFrames, routedDeck.assignedOutput, interleavedOutput_.data(), kFourDeckOutputChannels, mainGain);
        }
        if (deckResult.cueEnabled) {
            mixStereoToOutput(deckBuffer.data(), configuration.bufferFrames, snapshot.layout.cueOutput, interleavedOutput_.data(), kFourDeckOutputChannels, cueGain);
        }
        mixer_.publishDeckMeter(deckId, deckResult.peakMagnitude, deckResult.peakMagnitude);
        blockUnderruns += deckResult.underrunFrames;
        blockPeak = std::max(blockPeak, std::max(deckResult.peakMagnitude, pluginMetrics.peakMagnitude));
        metrics_.maxStretchLatencyFrames = std::max(metrics_.maxStretchLatencyFrames, deckResult.timeStretch.latencyFrames);
    }
    const auto masterMetrics = masterPluginChain_.processReplacing(masterPluginBuffer_.data(), configuration.bufferFrames, false);
    mixStereoToOutput(masterPluginBuffer_.data(), configuration.bufferFrames, snapshot.layout.masterOutput, interleavedOutput_.data(), kFourDeckOutputChannels, 1.0F);
    blockPeak = std::max(blockPeak, masterMetrics.peakMagnitude);

    const auto callbackEnd = std::chrono::steady_clock::now();
    const auto callbackMs = std::chrono::duration<double, std::milli>(callbackEnd - callbackStart).count();
    metrics_.renderedFrames += configuration.bufferFrames;
    ++metrics_.callbackCount;
    metrics_.underrunFrames += blockUnderruns;
    if (blockUnderruns > 0U) {
        ++metrics_.underrunCallbacks;
    }
    metrics_.maxCallbackMs = std::max(metrics_.maxCallbackMs, callbackMs);
    metrics_.peakMagnitude = std::max(metrics_.peakMagnitude, blockPeak);
    return FourDeckRenderResult{FourDeckPlaybackError::None, metrics_};
}

FourDeckRenderResult FourDeckPlaybackCore::renderOffline(const audio::AudioRenderConfiguration& configuration,
                                                         std::uint32_t blockCount) noexcept {
    FourDeckRenderResult result{FourDeckPlaybackError::None, metrics_};
    for (std::uint32_t block = 0; block < blockCount; ++block) {
        result = renderNextBlock(configuration);
        if (!result.ok()) {
            return result;
        }
    }
    return result;
}

bool FourDeckPlaybackCore::validDeckId(core::DeckId id) const noexcept {
    return id.index() < decks_.size();
}

bool FourDeckPlaybackCore::validRenderConfiguration(const audio::AudioRenderConfiguration& configuration) const noexcept {
    return configuration.sampleRateHz > 0U && configuration.bufferFrames > 0U && configuration.bufferFrames <= kFourDeckMaxRenderFrames;
}

const char* toString(FourDeckPlaybackError error) noexcept {
    switch (error) {
    case FourDeckPlaybackError::None:
        return "none";
    case FourDeckPlaybackError::InvalidDeckId:
        return "invalid-deck-id";
    case FourDeckPlaybackError::InvalidRenderConfiguration:
        return "invalid-render-configuration";
    case FourDeckPlaybackError::DeckLoadFailed:
        return "deck-load-failed";
    }
    return "invalid-render-configuration";
}

}
