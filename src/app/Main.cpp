#include "app/AlphaSmoke.h"
#include "app/Bootstrap.h"
#include "app/UiShell.h"

#include <iostream>

#if DJAPP_HAS_JUCE
#include <JuceHeader.h>

namespace {

bool juceCommandLineHas(const juce::String& commandLine, const char* flag) {
    return commandLine.contains(flag);
}

} // namespace

class DJApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "DJApp"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        const bool smokeTest = juceCommandLineHas(commandLine, "--smoke-test");
        const bool uiSmokeTest = juceCommandLineHas(commandLine, "--ui-smoke-test");
        const bool alphaSmokeTest = juceCommandLineHas(commandLine, "--alpha-smoke-test");
        const bool emptyLibrary = juceCommandLineHas(commandLine, "--empty-library");
        const bool exitAfterInit = juceCommandLineHas(commandLine, "--exit-after-init");
        const bool noAudioDevice = juceCommandLineHas(commandLine, "--no-audio-device");
        const auto bootstrap = djapp::app::initializeBootstrapServices(djapp::app::BootstrapOptions{smokeTest || uiSmokeTest || alphaSmokeTest, noAudioDevice});
        std::cout << djapp::app::formatBootstrapResult(bootstrap);

        if (!bootstrap.ok) {
            setApplicationReturnValue(1);
            quit();
            return;
        }

        if (uiSmokeTest && exitAfterInit) {
            setApplicationReturnValue(djapp::app::runUiSmokeTest(emptyLibrary));
            quit();
            return;
        }

        if (alphaSmokeTest && exitAfterInit) {
            setApplicationReturnValue(djapp::app::runAlphaSmokeTest(std::cout));
            quit();
            return;
        }

        if (smokeTest && exitAfterInit) {
            setApplicationReturnValue(0);
            quit();
            return;
        }

        std::cout << "DJApp JUCE bootstrap executable ready. Pass --smoke-test --exit-after-init, --ui-smoke-test --exit-after-init, or --alpha-smoke-test --exit-after-init for CI smoke validation.\n";
    }

    void shutdown() override {}
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
};

START_JUCE_APPLICATION(DJApplication)
#else
int main(int argc, char* argv[]) {
    const bool smokeTest = djapp::app::hasArgument(argc, argv, "--smoke-test");
    const bool uiSmokeTest = djapp::app::hasArgument(argc, argv, "--ui-smoke-test");
    const bool alphaSmokeTest = djapp::app::hasArgument(argc, argv, "--alpha-smoke-test");
    const bool emptyLibrary = djapp::app::hasArgument(argc, argv, "--empty-library");
    const bool exitAfterInit = djapp::app::hasArgument(argc, argv, "--exit-after-init");
    const bool noAudioDevice = djapp::app::hasArgument(argc, argv, "--no-audio-device");

    const auto bootstrap = djapp::app::initializeBootstrapServices(djapp::app::BootstrapOptions{smokeTest || uiSmokeTest || alphaSmokeTest, noAudioDevice});
    std::cout << djapp::app::formatBootstrapResult(bootstrap);

    if (!bootstrap.ok) {
        return 1;
    }

    if (uiSmokeTest && exitAfterInit) {
        return djapp::app::runUiSmokeTest(emptyLibrary);
    }

    if (alphaSmokeTest && exitAfterInit) {
        return djapp::app::runAlphaSmokeTest(std::cout);
    }

    if (smokeTest && exitAfterInit) {
        return 0;
    }

    std::cout << "DJApp bootstrap executable ready. Pass --smoke-test --exit-after-init, --ui-smoke-test --exit-after-init, or --alpha-smoke-test --exit-after-init for CI smoke validation.\n";
    return 0;
}
#endif
