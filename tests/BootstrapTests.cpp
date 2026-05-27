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
    const auto result = deckflaxia::app::initializeBootstrapServices(true);

    if (expect(result.ok, "bootstrap initialization should succeed") != 0) {
        return 1;
    }

    if (expect(result.initializedServices.size() == 4, "bootstrap should initialize bootstrap and deferred audio services") != 0) {
        return 1;
    }

    const auto output = deckflaxia::app::formatBootstrapResult(result);
    if (expect(output.find("configuration") != std::string::npos, "formatted output should include configuration service") != 0) {
        return 1;
    }
    if (expect(output.find("audio-device-state: deferred") != std::string::npos, "formatted output should include deferred audio state") != 0) {
        return 1;
    }

    const auto noDevice = deckflaxia::app::initializeBootstrapServices(deckflaxia::app::BootstrapOptions{true, true});
    if (expect(noDevice.audioDeviceState.status == deckflaxia::audio::AudioDeviceConnectionStatus::NoDeviceRequested,
               "no-device bootstrap should expose typed degraded state") != 0) {
        return 1;
    }
    if (expect(noDevice.audioDeviceState.degraded, "no-device bootstrap state should be degraded") != 0) {
        return 1;
    }

    char program[] = "DeckflaxiaTests";
    char smoke[] = "--smoke-test";
    char exitAfterInit[] = "--exit-after-init";
    char noAudio[] = "--no-audio-device";
    char juceShell[] = "--juce-shell-smoke-test";
    char* argv[] = {program, smoke, exitAfterInit, noAudio, juceShell};
    if (expect(deckflaxia::app::hasArgument(5, argv, "--exit-after-init"), "argument parser should detect exit flag") != 0) {
        return 1;
    }
    if (expect(deckflaxia::app::hasArgument(5, argv, "--no-audio-device"), "argument parser should detect no-device flag") != 0) {
        return 1;
    }
    if (expect(deckflaxia::app::hasArgument(5, argv, "--juce-shell-smoke-test"), "argument parser should detect JUCE shell smoke flag") != 0) {
        return 1;
    }

    std::cout << "Bootstrap tests passed\n";
    return 0;
}
