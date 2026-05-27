#include "app/Bootstrap.h"

#include <algorithm>
#include <sstream>

namespace deckflaxia::app {

BootstrapResult initializeBootstrapServices(const BootstrapOptions options) {
    auto audioService = options.noAudioDevice
                            ? audio::AudioDeviceService::noDevice(audio::currentBackendPolicy())
                            : audio::AudioDeviceService(audio::currentBackendPolicy());

    BootstrapResult result;
    result.ok = true;
    result.initializedServices = {"configuration", "logging", "application-state"};
    result.initializedServices.push_back(options.noAudioDevice ? "audio-device:no-device" : "audio-device:deferred");
    result.message = options.smokeTestMode ? "bootstrap smoke initialization complete" : "bootstrap initialization complete";
    result.audioDeviceState = audioService.state();
    return result;
}

BootstrapResult initializeBootstrapServices(const bool smokeTestMode) {
    return initializeBootstrapServices(BootstrapOptions{smokeTestMode, false});
}

bool hasArgument(const int argc, char* argv[], const std::string& expected) {
    return std::any_of(argv + 1, argv + argc, [&expected](const char* argument) {
        return argument != nullptr && expected == argument;
    });
}

std::string formatBootstrapResult(const BootstrapResult& result) {
    std::ostringstream output;
    output << result.message << '\n';
    output << "services:";
    for (const auto& service : result.initializedServices) {
        output << ' ' << service;
    }
    output << '\n';
    output << "audio-device-state: " << audio::toString(result.audioDeviceState.status)
           << " backend=" << audio::toString(result.audioDeviceState.backend)
           << " degraded=" << (result.audioDeviceState.degraded ? "true" : "false") << '\n';
    return output.str();
}

} // namespace deckflaxia::app
