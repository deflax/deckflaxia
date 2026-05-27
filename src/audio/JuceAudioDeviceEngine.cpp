#include "audio/JuceAudioDeviceEngine.h"

#if DJAPP_HAS_JUCE

#include <algorithm>
#include <memory>

namespace djapp::audio {

namespace {

bool supportedExtension(const juce::String& extension) {
    const auto lower = extension.toLowerCase();
    return lower == ".wav" || lower == ".aif" || lower == ".aiff" || lower == ".flac" || lower == ".mp3";
}

}

JuceAudioDeviceDeckEngine::JuceAudioDeviceDeckEngine() {
    formatManager_.registerBasicFormats();
}

JuceAudioDeviceDeckEngine::~JuceAudioDeviceDeckEngine() {
    detachFromDeviceManager();
}

decks::FourDeckPlaybackCore& JuceAudioDeviceDeckEngine::core() noexcept {
    return core_;
}

const decks::FourDeckPlaybackCore& JuceAudioDeviceDeckEngine::core() const noexcept {
    return core_;
}

JuceDeckLoadResult JuceAudioDeviceDeckEngine::loadFileToDeck(core::DeckId id, const std::filesystem::path& path) {
    if (id.index() >= routing::kDeckCount) {
        return {JuceAudioDeckError::InvalidDeckId, decks::AudioDeckLoadError::None};
    }

    const juce::File file(path.string());
    if (!file.existsAsFile()) {
        return {JuceAudioDeckError::MissingMedia, decks::AudioDeckLoadError::MissingMedia};
    }
    if (!supportedExtension(file.getFileExtension())) {
        return {JuceAudioDeckError::UnsupportedMedia, decks::AudioDeckLoadError::UnsupportedMedia};
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr) {
        const auto* format = formatManager_.findFormatForFileExtension(file.getFileExtension());
        const auto error = format == nullptr ? JuceAudioDeckError::UnsupportedMedia : JuceAudioDeckError::CorruptMedia;
        const auto deckError = format == nullptr ? decks::AudioDeckLoadError::UnsupportedMedia : decks::AudioDeckLoadError::CorruptMedia;
        return {error, deckError};
    }

    return prepareReaderMedia(id, path, *reader);
}

JuceAudioDeckError JuceAudioDeviceDeckEngine::attachToDeviceManager(juce::AudioDeviceManager& deviceManager) noexcept {
    detachFromDeviceManager();
    attachedDeviceManager_ = &deviceManager;
    attachedDeviceManager_->addAudioCallback(this);
    return JuceAudioDeckError::None;
}

void JuceAudioDeviceDeckEngine::detachFromDeviceManager() noexcept {
    if (attachedDeviceManager_ != nullptr) {
        attachedDeviceManager_->removeAudioCallback(this);
        attachedDeviceManager_ = nullptr;
    }
}

void JuceAudioDeviceDeckEngine::audioDeviceIOCallbackWithContext(const float* const*,
                                                                int,
                                                                float* const* outputChannelData,
                                                                int numOutputChannels,
                                                                int numSamples,
                                                                const juce::AudioIODeviceCallbackContext&) {
    for (int channel = 0; channel < numOutputChannels; ++channel) {
        if (outputChannelData[channel] != nullptr) {
            std::fill_n(outputChannelData[channel], static_cast<std::size_t>(numSamples), 0.0F);
        }
    }

    if (numSamples <= 0 || static_cast<std::uint32_t>(numSamples) > decks::kFourDeckMaxRenderFrames) {
        return;
    }

    callbackConfiguration_.bufferFrames = static_cast<std::uint32_t>(numSamples);
    const auto render = core_.renderNextBlock(callbackConfiguration_);
    if (!render.ok()) {
        return;
    }

    const auto& interleaved = core_.lastInterleavedOutput();
    const auto channelsToCopy = std::min<std::uint32_t>(static_cast<std::uint32_t>(std::max(numOutputChannels, 0)),
                                                       decks::kFourDeckOutputChannels);
    for (std::uint32_t frame = 0; frame < static_cast<std::uint32_t>(numSamples); ++frame) {
        const auto inputIndex = static_cast<std::size_t>(frame) * decks::kFourDeckOutputChannels;
        for (std::uint32_t channel = 0; channel < channelsToCopy; ++channel) {
            if (outputChannelData[channel] != nullptr) {
                outputChannelData[channel][frame] = interleaved[inputIndex + channel];
            }
        }
    }
}

void JuceAudioDeviceDeckEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device != nullptr) {
        callbackConfiguration_.sampleRateHz = static_cast<std::uint32_t>(device->getCurrentSampleRate());
        callbackConfiguration_.bufferFrames = static_cast<std::uint32_t>(device->getCurrentBufferSizeSamples());
    }
}

void JuceAudioDeviceDeckEngine::audioDeviceStopped() {}

JuceDeckLoadResult JuceAudioDeviceDeckEngine::prepareReaderMedia(core::DeckId id,
                                                                 const std::filesystem::path& path,
                                                                 juce::AudioFormatReader& reader) {
    if (reader.lengthInSamples <= 0) {
        return {JuceAudioDeckError::EmptyMedia, decks::AudioDeckLoadError::EmptyMedia};
    }
    if (reader.lengthInSamples > static_cast<juce::int64>(decks::kMaxPreparedAudioFrames)) {
        return {JuceAudioDeckError::TooManyFrames, decks::AudioDeckLoadError::TooManyFrames};
    }

    const auto frameCount = static_cast<int>(reader.lengthInSamples);
    juce::AudioBuffer<float> decodeBuffer(2, frameCount);
    decodeBuffer.clear();
    if (!reader.read(&decodeBuffer, 0, frameCount, 0, true, true)) {
        return {JuceAudioDeckError::CorruptMedia, decks::AudioDeckLoadError::CorruptMedia};
    }

    decks::PreparedAudioMedia media;
    media.frameCount = static_cast<std::uint32_t>(frameCount);
    media.sampleRateHz = static_cast<std::uint32_t>(reader.sampleRate > 0.0 ? reader.sampleRate : 44100.0);
    media.label = path.filename().string();
    media.interleavedSamples.resize(static_cast<std::size_t>(media.frameCount) * decks::kAudioDeckChannels);
    const auto* left = decodeBuffer.getReadPointer(0);
    const auto* right = decodeBuffer.getNumChannels() > 1 ? decodeBuffer.getReadPointer(1) : left;
    for (std::uint32_t frame = 0; frame < media.frameCount; ++frame) {
        media.interleavedSamples[(static_cast<std::size_t>(frame) * decks::kAudioDeckChannels)] = left[frame];
        media.interleavedSamples[(static_cast<std::size_t>(frame) * decks::kAudioDeckChannels) + 1U] = right[frame];
    }

    const auto load = core_.loadDeck(id, decks::AudioDeckMediaReference::preparedAudio(std::move(media)));
    if (!load.ok()) {
        return {mapDeckLoadError(load.deckError), load.deckError};
    }
    return {};
}

}

#endif

namespace djapp::audio {

const char* toString(JuceAudioDeckError error) noexcept {
    switch (error) {
    case JuceAudioDeckError::None:
        return "none";
    case JuceAudioDeckError::JuceUnavailable:
        return "juce-unavailable";
    case JuceAudioDeckError::MissingMedia:
        return "missing-media";
    case JuceAudioDeckError::UnsupportedMedia:
        return "unsupported-media";
    case JuceAudioDeckError::CorruptMedia:
        return "corrupt-media";
    case JuceAudioDeckError::EmptyMedia:
        return "empty-media";
    case JuceAudioDeckError::TooManyFrames:
        return "too-many-frames";
    case JuceAudioDeckError::InvalidDeckId:
        return "invalid-deck-id";
    case JuceAudioDeckError::DeviceError:
        return "device-error";
    }
    return "device-error";
}

JuceAudioDeckError mapDeckLoadError(decks::AudioDeckLoadError error) noexcept {
    switch (error) {
    case decks::AudioDeckLoadError::None:
        return JuceAudioDeckError::None;
    case decks::AudioDeckLoadError::MissingMedia:
        return JuceAudioDeckError::MissingMedia;
    case decks::AudioDeckLoadError::CorruptMedia:
        return JuceAudioDeckError::CorruptMedia;
    case decks::AudioDeckLoadError::UnsupportedMedia:
        return JuceAudioDeckError::UnsupportedMedia;
    case decks::AudioDeckLoadError::EmptyMedia:
        return JuceAudioDeckError::EmptyMedia;
    case decks::AudioDeckLoadError::TooManyFrames:
        return JuceAudioDeckError::TooManyFrames;
    }
    return JuceAudioDeckError::DeviceError;
}

}
