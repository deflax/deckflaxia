#pragma once

#include "decks/FourDeckPlaybackCore.h"

#include <filesystem>

#ifndef DECKFLAXIA_HAS_JUCE
#define DECKFLAXIA_HAS_JUCE 0
#endif

#if DECKFLAXIA_HAS_JUCE
#include <juce_audio_utils/juce_audio_utils.h>
#endif

namespace deckflaxia::audio {

enum class JuceAudioDeckError : std::uint8_t {
    None,
    JuceUnavailable,
    MissingMedia,
    UnsupportedMedia,
    CorruptMedia,
    EmptyMedia,
    TooManyFrames,
    InvalidDeckId,
    DeviceError,
};

struct JuceDeckLoadResult final {
    JuceAudioDeckError error{JuceAudioDeckError::None};
    decks::AudioDeckLoadError deckError{decks::AudioDeckLoadError::None};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == JuceAudioDeckError::None && deckError == decks::AudioDeckLoadError::None;
    }
};

#if DECKFLAXIA_HAS_JUCE
class JuceAudioDeviceDeckEngine final : public juce::AudioIODeviceCallback {
public:
    JuceAudioDeviceDeckEngine();
    ~JuceAudioDeviceDeckEngine() override;

    JuceAudioDeviceDeckEngine(const JuceAudioDeviceDeckEngine&) = delete;
    JuceAudioDeviceDeckEngine& operator=(const JuceAudioDeviceDeckEngine&) = delete;

    [[nodiscard]] decks::FourDeckPlaybackCore& core() noexcept;
    [[nodiscard]] const decks::FourDeckPlaybackCore& core() const noexcept;
    [[nodiscard]] JuceDeckLoadResult loadFileToDeck(core::DeckId id, const std::filesystem::path& path);
    [[nodiscard]] JuceAudioDeckError attachToDeviceManager(juce::AudioDeviceManager& deviceManager) noexcept;
    void detachFromDeviceManager() noexcept;

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    [[nodiscard]] JuceDeckLoadResult prepareReaderMedia(core::DeckId id,
                                                        const std::filesystem::path& path,
                                                        juce::AudioFormatReader& reader);

    juce::AudioFormatManager formatManager_;
    juce::AudioDeviceManager* attachedDeviceManager_{};
    decks::FourDeckPlaybackCore core_;
    AudioRenderConfiguration callbackConfiguration_{48000, 512};
};
#endif

[[nodiscard]] const char* toString(JuceAudioDeckError error) noexcept;
[[nodiscard]] JuceAudioDeckError mapDeckLoadError(decks::AudioDeckLoadError error) noexcept;

}
