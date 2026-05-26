#pragma once

#include "audio/AudioGraphContracts.h"

#include <cstdint>

namespace djapp::audio {

enum class HostAudioPlatform {
    MacOS,
    Linux,
    Windows,
    Other,
};

enum class AudioBackendKind {
    CoreAudio,
    Jack,
    Alsa,
    PortableDeferred,
    Unknown,
};

struct AudioBackendPolicy final {
    HostAudioPlatform platform{HostAudioPlatform::Other};
    AudioBackendKind preferred{AudioBackendKind::PortableDeferred};
    AudioBackendKind fallback{AudioBackendKind::Unknown};
    bool alphaVerified{false};
};

enum class AudioDeviceConnectionStatus {
    Deferred,
    NoDeviceRequested,
    MissingDevice,
    Disconnected,
    SampleRateChanged,
    Running,
};

enum class AudioDeviceEventKind {
    DeviceMissing,
    DeviceDisconnected,
    SampleRateChanged,
    DeviceRunning,
};

struct AudioDeviceState final {
    AudioDeviceConnectionStatus status{AudioDeviceConnectionStatus::Deferred};
    AudioBackendKind backend{AudioBackendKind::PortableDeferred};
    std::uint32_t sampleRateHz{};
    std::uint32_t bufferFrames{};
    bool degraded{true};
};

struct AudioRenderConfiguration final {
    std::uint32_t sampleRateHz{44100};
    std::uint32_t bufferFrames{64};
};

struct OfflineRenderResult final {
    std::uint64_t renderedFrames{};
    std::uint32_t renderedBlocks{};
    float peakMagnitude{};
};

HostAudioPlatform currentHostAudioPlatform() noexcept;
AudioBackendPolicy backendPolicyForPlatform(HostAudioPlatform platform) noexcept;
AudioBackendPolicy currentBackendPolicy() noexcept;
bool isAlphaVerifiedRenderConfiguration(const AudioRenderConfiguration& configuration) noexcept;

class AudioDeviceService final {
public:
    explicit AudioDeviceService(AudioBackendPolicy policy) noexcept;

    [[nodiscard]] static AudioDeviceService noDevice(AudioBackendPolicy policy) noexcept;

    [[nodiscard]] const AudioDeviceState& state() const noexcept;
    [[nodiscard]] AudioBackendPolicy policy() const noexcept;

    void markMissingDevice(AudioRenderConfiguration requested) noexcept;
    void applyEvent(AudioDeviceEventKind event, AudioRenderConfiguration configuration) noexcept;

private:
    AudioBackendPolicy policy_{};
    AudioDeviceState state_{};
};

class OfflineAudioRenderer final {
public:
    [[nodiscard]] OfflineRenderResult renderSilent(const AudioRenderConfiguration& configuration,
                                                   std::uint32_t blockCount) noexcept;
};

const char* toString(AudioBackendKind backend) noexcept;
const char* toString(AudioDeviceConnectionStatus status) noexcept;

}
