#include "app/Bootstrap.h"

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

} // namespace

int main() {
    const auto result = djapp::app::initializeBootstrapServices(true);

    if (expect(result.ok, "bootstrap initialization should succeed") != 0) {
        return 1;
    }

    if (expect(result.initializedServices.size() == 4, "bootstrap should initialize bootstrap and deferred audio services") != 0) {
        return 1;
    }

    const auto output = djapp::app::formatBootstrapResult(result);
    if (expect(output.find("configuration") != std::string::npos, "formatted output should include configuration service") != 0) {
        return 1;
    }
    if (expect(output.find("audio-device-state: deferred") != std::string::npos, "formatted output should include deferred audio state") != 0) {
        return 1;
    }

    const auto noDevice = djapp::app::initializeBootstrapServices(djapp::app::BootstrapOptions{true, true});
    if (expect(noDevice.audioDeviceState.status == djapp::audio::AudioDeviceConnectionStatus::NoDeviceRequested,
               "no-device bootstrap should expose typed degraded state") != 0) {
        return 1;
    }
    if (expect(noDevice.audioDeviceState.degraded, "no-device bootstrap state should be degraded") != 0) {
        return 1;
    }

    char program[] = "DJAppTests";
    char smoke[] = "--smoke-test";
    char exitAfterInit[] = "--exit-after-init";
    char noAudio[] = "--no-audio-device";
    char* argv[] = {program, smoke, exitAfterInit, noAudio};
    if (expect(djapp::app::hasArgument(4, argv, "--exit-after-init"), "argument parser should detect exit flag") != 0) {
        return 1;
    }
    if (expect(djapp::app::hasArgument(4, argv, "--no-audio-device"), "argument parser should detect no-device flag") != 0) {
        return 1;
    }

    std::cout << "Bootstrap tests passed\n";
    return 0;
}
