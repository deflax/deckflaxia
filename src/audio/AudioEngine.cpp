#include "audio/AudioEngine.h"

namespace deckflaxia::audio {

HostAudioPlatform currentHostAudioPlatform() noexcept {
#if defined(__APPLE__)
    return HostAudioPlatform::MacOS;
#elif defined(__linux__)
    return HostAudioPlatform::Linux;
#elif defined(_WIN32)
    return HostAudioPlatform::Windows;
#else
    return HostAudioPlatform::Other;
#endif
}

AudioBackendPolicy backendPolicyForPlatform(const HostAudioPlatform platform) noexcept {
    switch (platform) {
    case HostAudioPlatform::MacOS:
        return AudioBackendPolicy{platform, AudioBackendKind::CoreAudio, AudioBackendKind::Unknown, true};
    case HostAudioPlatform::Linux:
        return AudioBackendPolicy{platform, AudioBackendKind::Jack, AudioBackendKind::Alsa, true};
    case HostAudioPlatform::Windows:
        return AudioBackendPolicy{platform, AudioBackendKind::PortableDeferred, AudioBackendKind::Unknown, false};
    case HostAudioPlatform::Other:
        return AudioBackendPolicy{platform, AudioBackendKind::PortableDeferred, AudioBackendKind::Unknown, false};
    }
    return AudioBackendPolicy{HostAudioPlatform::Other, AudioBackendKind::PortableDeferred, AudioBackendKind::Unknown, false};
}

AudioBackendPolicy currentBackendPolicy() noexcept {
    return backendPolicyForPlatform(currentHostAudioPlatform());
}

bool isAlphaVerifiedRenderConfiguration(const AudioRenderConfiguration& configuration) noexcept {
    for (const auto& pair : kAlphaSampleRateBufferMatrix) {
        if (pair.sampleRateHz == configuration.sampleRateHz && pair.bufferFrames == configuration.bufferFrames) {
            return true;
        }
    }
    return false;
}

AudioDeviceService::AudioDeviceService(const AudioBackendPolicy policy) noexcept
    : policy_(policy),
      state_(AudioDeviceState{AudioDeviceConnectionStatus::Deferred, policy.preferred, 0, 0, true}) {}

AudioDeviceService AudioDeviceService::noDevice(const AudioBackendPolicy policy) noexcept {
    AudioDeviceService service(policy);
    service.state_ = AudioDeviceState{AudioDeviceConnectionStatus::NoDeviceRequested, policy.preferred, 0, 0, true};
    return service;
}

const AudioDeviceState& AudioDeviceService::state() const noexcept {
    return state_;
}

AudioBackendPolicy AudioDeviceService::policy() const noexcept {
    return policy_;
}

void AudioDeviceService::markMissingDevice(const AudioRenderConfiguration requested) noexcept {
    state_ = AudioDeviceState{AudioDeviceConnectionStatus::MissingDevice,
                              policy_.preferred,
                              requested.sampleRateHz,
                              requested.bufferFrames,
                              true};
}

void AudioDeviceService::applyEvent(const AudioDeviceEventKind event, const AudioRenderConfiguration configuration) noexcept {
    switch (event) {
    case AudioDeviceEventKind::DeviceMissing:
        markMissingDevice(configuration);
        return;
    case AudioDeviceEventKind::DeviceDisconnected:
        state_ = AudioDeviceState{AudioDeviceConnectionStatus::Disconnected,
                                  state_.backend,
                                  state_.sampleRateHz,
                                  state_.bufferFrames,
                                  true};
        return;
    case AudioDeviceEventKind::SampleRateChanged:
        state_ = AudioDeviceState{AudioDeviceConnectionStatus::SampleRateChanged,
                                  state_.backend,
                                  configuration.sampleRateHz,
                                  configuration.bufferFrames,
                                  false};
        return;
    case AudioDeviceEventKind::DeviceRunning:
        state_ = AudioDeviceState{AudioDeviceConnectionStatus::Running,
                                  policy_.preferred,
                                  configuration.sampleRateHz,
                                  configuration.bufferFrames,
                                  false};
        return;
    }
}

OfflineRenderResult OfflineAudioRenderer::renderSilent(const AudioRenderConfiguration& configuration,
                                                       const std::uint32_t blockCount) noexcept {
    return OfflineRenderResult{static_cast<std::uint64_t>(configuration.bufferFrames) * blockCount, blockCount, 0.0F};
}

const char* toString(const AudioBackendKind backend) noexcept {
    switch (backend) {
    case AudioBackendKind::CoreAudio:
        return "coreaudio";
    case AudioBackendKind::Jack:
        return "jack";
    case AudioBackendKind::Alsa:
        return "alsa";
    case AudioBackendKind::PortableDeferred:
        return "portable-deferred";
    case AudioBackendKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* toString(const AudioDeviceConnectionStatus status) noexcept {
    switch (status) {
    case AudioDeviceConnectionStatus::Deferred:
        return "deferred";
    case AudioDeviceConnectionStatus::NoDeviceRequested:
        return "no-device-requested";
    case AudioDeviceConnectionStatus::MissingDevice:
        return "missing-device";
    case AudioDeviceConnectionStatus::Disconnected:
        return "disconnected";
    case AudioDeviceConnectionStatus::SampleRateChanged:
        return "sample-rate-changed";
    case AudioDeviceConnectionStatus::Running:
        return "running";
    }
    return "deferred";
}

}
