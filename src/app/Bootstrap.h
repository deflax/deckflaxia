#pragma once

#include "audio/AudioEngine.h"

#include <string>
#include <vector>

namespace deckflaxia::app {

struct BootstrapOptions {
    bool smokeTestMode{};
    bool noAudioDevice{};
};

struct BootstrapResult {
    bool ok{};
    std::vector<std::string> initializedServices;
    std::string message;
    audio::AudioDeviceState audioDeviceState;
};

BootstrapResult initializeBootstrapServices(BootstrapOptions options);
BootstrapResult initializeBootstrapServices(bool smokeTestMode);
bool hasArgument(int argc, char* argv[], const std::string& expected);
std::string formatBootstrapResult(const BootstrapResult& result);

} // namespace deckflaxia::app
