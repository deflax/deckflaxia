#include "audio/AudioEngine.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int testBackendPolicy() {
    using namespace djapp::audio;

    const auto mac = backendPolicyForPlatform(HostAudioPlatform::MacOS);
    if (expect(mac.preferred == AudioBackendKind::CoreAudio, "macOS should prefer CoreAudio") != 0) {
        return 1;
    }
    if (expect(mac.alphaVerified, "macOS CoreAudio policy should be alpha verified") != 0) {
        return 1;
    }

    const auto linux = backendPolicyForPlatform(HostAudioPlatform::Linux);
    if (expect(linux.preferred == AudioBackendKind::Jack, "Linux should prefer JACK") != 0) {
        return 1;
    }
    if (expect(linux.fallback == AudioBackendKind::Alsa, "Linux should fall back to ALSA") != 0) {
        return 1;
    }
    if (expect(linux.alphaVerified, "Linux JACK/ALSA policy should be alpha verified") != 0) {
        return 1;
    }

    const auto windows = backendPolicyForPlatform(HostAudioPlatform::Windows);
    if (expect(windows.preferred == AudioBackendKind::PortableDeferred, "Windows should use portable deferred policy") != 0) {
        return 1;
    }
    return expect(!windows.alphaVerified, "Windows policy should not claim alpha verification");
}

int testDeviceEvents() {
    using namespace djapp::audio;

    auto service = AudioDeviceService(backendPolicyForPlatform(HostAudioPlatform::Linux));
    service.markMissingDevice(AudioRenderConfiguration{44100, 128});
    if (expect(service.state().status == AudioDeviceConnectionStatus::MissingDevice, "missing device should be typed") != 0) {
        return 1;
    }
    if (expect(service.state().degraded, "missing device should be degraded") != 0) {
        return 1;
    }

    service.applyEvent(AudioDeviceEventKind::DeviceRunning, AudioRenderConfiguration{44100, 128});
    if (expect(service.state().status == AudioDeviceConnectionStatus::Running, "running device should be typed") != 0) {
        return 1;
    }
    if (expect(!service.state().degraded, "running device should not be degraded") != 0) {
        return 1;
    }

    service.applyEvent(AudioDeviceEventKind::SampleRateChanged, AudioRenderConfiguration{48000, 512});
    if (expect(service.state().status == AudioDeviceConnectionStatus::SampleRateChanged, "sample-rate change should be typed") != 0) {
        return 1;
    }
    if (expect(service.state().sampleRateHz == 48000 && service.state().bufferFrames == 512,
               "sample-rate change should update configuration") != 0) {
        return 1;
    }

    service.applyEvent(AudioDeviceEventKind::DeviceDisconnected, AudioRenderConfiguration{48000, 512});
    return expect(service.state().status == AudioDeviceConnectionStatus::Disconnected, "disconnect should be typed");
}

int testNoDeviceState() {
    using namespace djapp::audio;

    const auto service = AudioDeviceService::noDevice(backendPolicyForPlatform(HostAudioPlatform::Linux));
    if (expect(service.state().status == AudioDeviceConnectionStatus::NoDeviceRequested, "no-device request should be typed") != 0) {
        return 1;
    }
    if (expect(service.state().degraded, "no-device request should be degraded") != 0) {
        return 1;
    }
    return expect(service.state().backend == AudioBackendKind::Jack, "no-device state should retain backend policy");
}

int testOfflineMatrix() {
    using namespace djapp::audio;

    OfflineAudioRenderer renderer;
    for (const auto& pair : kAlphaSampleRateBufferMatrix) {
        const AudioRenderConfiguration configuration{pair.sampleRateHz, pair.bufferFrames};
        if (expect(isAlphaVerifiedRenderConfiguration(configuration), "matrix pair should be alpha verified") != 0) {
            return 1;
        }
        const auto result = renderer.renderSilent(configuration, 3);
        if (expect(result.renderedFrames == static_cast<std::uint64_t>(pair.bufferFrames) * 3,
                   "offline renderer should report deterministic frame count") != 0) {
            return 1;
        }
        if (expect(result.renderedBlocks == 3, "offline renderer should report deterministic block count") != 0) {
            return 1;
        }
        if (expect(std::abs(result.peakMagnitude) < 0.000001F, "silent render should remain silent") != 0) {
            return 1;
        }
    }

    return expect(!isAlphaVerifiedRenderConfiguration(AudioRenderConfiguration{96000, 64}),
                  "non-matrix sample rate should not be alpha verified");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "matrix") {
        return testOfflineMatrix();
    }
    if (filter == "policy") {
        return testBackendPolicy();
    }
    if (filter == "events") {
        return testDeviceEvents();
    }
    if (filter == "no-device") {
        return testNoDeviceState();
    }

    if (testBackendPolicy() != 0 || testDeviceEvents() != 0 || testNoDeviceState() != 0 || testOfflineMatrix() != 0) {
        return 1;
    }

    std::cout << "Audio engine tests passed\n";
    return 0;
}
