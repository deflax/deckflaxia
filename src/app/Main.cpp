#include "app/AlphaSmoke.h"
#include "app/Bootstrap.h"
#include "app/PlayableSmoke.h"
#include "app/UiShell.h"
#include "audio/AudioDeckSmoke.h"
#include "library/AudioImport.h"
#include "plugins/PluginSandbox.h"
#include "ui/BrowserWaveformBeatgrid.h"
#include "ui/JuceComponentTree.h"
#include "ui/VST3EditorUi.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#if DECKFLAXIA_HAS_JUCE
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace {

bool juceCommandLineHas(const juce::String& commandLine, const char* flag) {
    return commandLine.contains(flag);
}

std::filesystem::path juceFixtureDirectory(const juce::String& commandLine) {
    const auto tokens = juce::StringArray::fromTokens(commandLine, true);
    for (int index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index] == "--fixtures") {
            return std::filesystem::path(tokens[index + 1].toStdString());
        }
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

std::optional<std::filesystem::path> juceCommandLinePathAfter(const juce::String& commandLine, const char* flag) {
    const auto tokens = juce::StringArray::fromTokens(commandLine, true);
    for (int index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index] == flag) {
            return std::filesystem::path(tokens[index + 1].toStdString());
        }
    }
    return std::nullopt;
}


std::vector<std::filesystem::path> juceCommandLinePathsAfter(const juce::String& commandLine, const char* flag) {
    std::vector<std::filesystem::path> paths;
    const auto tokens = juce::StringArray::fromTokens(commandLine, true);
    for (int index = 0; index < tokens.size(); ++index) {
        if (tokens[index] != flag) {
            continue;
        }
        for (int pathIndex = index + 1; pathIndex < tokens.size() && !tokens[pathIndex].startsWith("--"); ++pathIndex) {
            paths.emplace_back(tokens[pathIndex].toStdString());
        }
        break;
    }
    return paths;
}


std::uint64_t juceCommandLineUnsignedAfter(const juce::String& commandLine, const char* flag) {
    const auto tokens = juce::StringArray::fromTokens(commandLine, true);
    for (int index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index] == flag) {
            return static_cast<std::uint64_t>(std::strtoull(tokens[index + 1].toRawUTF8(), nullptr, 10));
        }
    }
    return 0U;
}

std::filesystem::path juceSandboxHelperPath() {
    return std::filesystem::path(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("DeckflaxiaPluginSandboxHelper").getFullPathName().toStdString());
}

std::vector<deckflaxia::library::FilesystemEntry> juceBrowserWaveformFixtures(const std::filesystem::path& fixtureDir) {
    return {{(fixtureDir / "track_120bpm.wav").string(), true},
            {(fixtureDir / "track_128bpm.wav").string(), true},
            {(fixtureDir / "track_95bpm.mp3").string(), true},
            {(fixtureDir / "corrupt_audio.wav").string(), true},
            {(fixtureDir / "not_audio.txt").string(), true}};
}

class MainWindow final : public juce::DocumentWindow {
public:
    explicit MainWindow(const juce::String& name, bool noAudioDevice, bool showWindow)
        : juce::DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(new deckflaxia::ui::MainComponent(noAudioDevice), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(showWindow);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    [[nodiscard]] deckflaxia::ui::MainComponent* mainComponent() const noexcept { return dynamic_cast<deckflaxia::ui::MainComponent*>(getContentComponent()); }
};

void writeJuceShellSmokeReport(const MainWindow* window) {
    const auto* component = window == nullptr ? nullptr : window->mainComponent();
    std::cout << "juce-shell-smoke-test: " << (window != nullptr && component != nullptr ? "ok" : "fail") << '\n';
    std::cout << "main-window: " << (window != nullptr ? "present" : "missing") << '\n';
    std::cout << "main-component: " << (component != nullptr ? "present" : "missing") << '\n';
    std::cout << "audio-device-manager: " << (component != nullptr && component->audioDeviceManagerInitialized() ? "initialized" : "missing") << '\n';
    std::cout << "command-manager: " << (component != nullptr && component->commandManagerPresent() ? "present" : "missing") << '\n';
}

} // namespace

class DeckflaxiaApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Deckflaxia"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        const bool smokeTest = juceCommandLineHas(commandLine, "--smoke-test");
        const bool uiSmokeTest = juceCommandLineHas(commandLine, "--ui-smoke-test");
        const bool alphaSmokeTest = juceCommandLineHas(commandLine, "--alpha-smoke-test");
        const bool juceShellSmokeTest = juceCommandLineHas(commandLine, "--juce-shell-smoke-test");
        const bool juceUiSmokeTest = juceCommandLineHas(commandLine, "--juce-ui-smoke-test");
        const bool dumpComponents = juceCommandLineHas(commandLine, "--dump-components");
        const bool audioDeckSmokeTest = juceCommandLineHas(commandLine, "--audio-deck-smoke-test");
        const bool tempoSyncSmokeTest = juceCommandLineHas(commandLine, "--tempo-sync-smoke-test");
        const bool timeStretchOverloadSmokeTest = juceCommandLineHas(commandLine, "--time-stretch-overload-smoke-test");
        const bool mixerSmokeTest = juceCommandLineHas(commandLine, "--mixer-smoke-test");
        const bool vst3ProcessingSmokeTest = juceCommandLineHas(commandLine, "--vst3-processing-smoke-test");
        const bool vst3EditorSmokeTest = juceCommandLineHas(commandLine, "--vst3-editor-smoke-test");
        const bool pluginSandboxSmokeTest = juceCommandLineHas(commandLine, "--plugin-sandbox-smoke-test");
        const bool playableSmokeTest = juceCommandLineHas(commandLine, "--playable-smoke-test");
        const bool performanceSmokeTest = juceCommandLineHas(commandLine, "--performance-smoke-test");
        const bool productionDjWorkflowSmokeTest = juceCommandLineHas(commandLine, "--production-dj-workflow-smoke-test");
        const bool scopeAudit = juceCommandLineHas(commandLine, "--scope-audit");
        const bool browserWaveformSmokeTest = juceCommandLineHas(commandLine, "--browser-waveform-smoke-test");
        const bool beatgridEditSmokeTest = juceCommandLineHas(commandLine, "--beatgrid-edit-smoke-test");
        const bool emptyLibrary = juceCommandLineHas(commandLine, "--empty-library");
        const bool exitAfterInit = juceCommandLineHas(commandLine, "--exit-after-init");
        const bool noAudioDevice = juceCommandLineHas(commandLine, "--no-audio-device");
        const auto screenshotPath = juceCommandLinePathAfter(commandLine, "--screenshot");
        const auto renderPath = juceCommandLinePathAfter(commandLine, "--render");
        const auto databasePath = juceCommandLinePathAfter(commandLine, "--db");
        const auto chainName = juceCommandLinePathAfter(commandLine, "--chain");
        const auto killHelperAfterMs = juceCommandLineUnsignedAfter(commandLine, "--kill-helper-after-ms");
        const auto sampleRateHz = static_cast<std::uint32_t>(juceCommandLineUnsignedAfter(commandLine, "--sample-rate"));
        const auto bufferFrames = static_cast<std::uint32_t>(juceCommandLineUnsignedAfter(commandLine, "--buffer-size"));
        const auto bootstrap = deckflaxia::app::initializeBootstrapServices(deckflaxia::app::BootstrapOptions{smokeTest || uiSmokeTest || alphaSmokeTest || juceShellSmokeTest || juceUiSmokeTest || audioDeckSmokeTest || tempoSyncSmokeTest || timeStretchOverloadSmokeTest || mixerSmokeTest || vst3ProcessingSmokeTest || vst3EditorSmokeTest || pluginSandboxSmokeTest || playableSmokeTest || performanceSmokeTest || productionDjWorkflowSmokeTest || scopeAudit || browserWaveformSmokeTest || beatgridEditSmokeTest, noAudioDevice});
        std::cout << deckflaxia::app::formatBootstrapResult(bootstrap);

        if (!bootstrap.ok) {
            setApplicationReturnValue(1);
            quit();
            return;
        }

        if (uiSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::app::runUiSmokeTest(emptyLibrary));
            quit();
            return;
        }

        if (alphaSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::app::runAlphaSmokeTest(std::cout));
            quit();
            return;
        }

        if (audioDeckSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::audio::runAudioDeckSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{juceFixtureDirectory(commandLine)}));
            quit();
            return;
        }

        if (tempoSyncSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::audio::runTempoSyncSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{juceFixtureDirectory(commandLine)}));
            quit();
            return;
        }

        if (timeStretchOverloadSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::audio::runTimeStretchOverloadSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{juceFixtureDirectory(commandLine)}));
            quit();
            return;
        }

        if (mixerSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::audio::runMixerSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{juceFixtureDirectory(commandLine), renderPath.value_or(std::filesystem::path{})}));
            quit();
            return;
        }

        if (vst3ProcessingSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::audio::runVst3ProcessingSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{juceFixtureDirectory(commandLine), renderPath.value_or(std::filesystem::path{}), chainName.has_value() ? chainName->string() : std::string{"deck-a"}}));
            quit();
            return;
        }

        if (vst3EditorSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::ui::runVst3EditorSmokeTest(std::cout, deckflaxia::ui::VST3EditorSmokeOptions{juceFixtureDirectory(commandLine), screenshotPath.value_or(std::filesystem::path{})}));
            quit();
            return;
        }

        if (pluginSandboxSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::plugins::runPluginSandboxSmokeTest(std::cout, deckflaxia::plugins::PluginSandboxSmokeOptions{juceFixtureDirectory(commandLine), juceSandboxHelperPath(), killHelperAfterMs}));
            quit();
            return;
        }

        if (playableSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::app::runPlayableSmokeTest(std::cout, deckflaxia::app::PlayableSmokeOptions{juceFixtureDirectory(commandLine), screenshotPath.value_or(std::filesystem::path{}), renderPath.value_or(std::filesystem::path{}), databasePath.value_or(std::filesystem::path{}), juceSandboxHelperPath(), juceCommandLineHas(commandLine, "--expect-restored-session")}));
            quit();
            return;
        }

        if (performanceSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::app::runPerformanceSmokeTest(std::cout, deckflaxia::app::PerformanceSmokeOptions{juceFixtureDirectory(commandLine), std::filesystem::path{".omo/evidence/real-playable-juce/task-15-performance.json"}, sampleRateHz == 0U ? 48000U : sampleRateHz, bufferFrames == 0U ? 512U : bufferFrames}));
            quit();
            return;
        }

        if (productionDjWorkflowSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::app::runProductionDjWorkflowSmokeTest(std::cout, deckflaxia::app::ProductionDjWorkflowSmokeOptions{juceFixtureDirectory(commandLine), std::filesystem::path{".omo/evidence/real-playable-juce/task-16-production-smoke.log"}, databasePath.value_or(std::filesystem::path{".omo/evidence/real-playable-juce/task-16-restart.db"}), sampleRateHz == 0U ? 48000U : sampleRateHz, bufferFrames == 0U ? 512U : bufferFrames}));
            quit();
            return;
        }

        if (scopeAudit) {
            const auto forbid = juceCommandLinePathAfter(commandLine, "--forbid");
            auto paths = juceCommandLinePathsAfter(commandLine, "--paths");
            setApplicationReturnValue(deckflaxia::app::runScopeAudit(std::cout, deckflaxia::app::ScopeAuditOptions{forbid.has_value() ? deckflaxia::app::splitScopeAuditTerms(forbid->string()) : std::vector<std::string>{}, paths.empty() ? std::vector<std::filesystem::path>{"src", "tests", "docs", "cmake"} : paths, std::filesystem::path{".omo/evidence/real-playable-juce/task-16-scope-audit.log"}}));
            quit();
            return;
        }

        if (browserWaveformSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::ui::runBrowserWaveformSmokeTest(std::cout, juceBrowserWaveformFixtures(juceFixtureDirectory(commandLine))));
            quit();
            return;
        }

        if (beatgridEditSmokeTest && exitAfterInit) {
            setApplicationReturnValue(deckflaxia::ui::runBeatgridEditSmokeTest(std::cout));
            quit();
            return;
        }

        const bool exitSmokeLaunch = exitAfterInit && (smokeTest || juceShellSmokeTest || juceUiSmokeTest);
        const bool noAudioForComponentTreeSmoke = noAudioDevice || (exitAfterInit && juceUiSmokeTest);
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), noAudioForComponentTreeSmoke, !exitSmokeLaunch);

        if (juceUiSmokeTest && exitAfterInit) {
            auto* component = mainWindow_->mainComponent();
            if (component == nullptr) {
                setApplicationReturnValue(1);
                mainWindow_.reset();
                quit();
                return;
            }
            if (dumpComponents) {
                deckflaxia::ui::writeComponentTreeReport(*component, std::cout);
            }
            bool ok = true;
            if (screenshotPath.has_value()) {
                ok = deckflaxia::ui::writeComponentScreenshot(*component, *screenshotPath, std::cout);
            }
            setApplicationReturnValue(ok ? 0 : 1);
            mainWindow_.reset();
            quit();
            return;
        }

        if (juceShellSmokeTest && exitAfterInit) {
            writeJuceShellSmokeReport(mainWindow_.get());
            const auto* component = mainWindow_->mainComponent();
            setApplicationReturnValue(component != nullptr && component->audioDeviceManagerInitialized() ? 0 : 1);
            mainWindow_.reset();
            quit();
            return;
        }

        if (smokeTest && exitAfterInit) {
            setApplicationReturnValue(0);
            mainWindow_.reset();
            quit();
            return;
        }

        std::cout << "Deckflaxia JUCE shell ready. Pass --smoke-test --exit-after-init, --ui-smoke-test --exit-after-init, --alpha-smoke-test --exit-after-init, --audio-deck-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --tempo-sync-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --time-stretch-overload-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --mixer-smoke-test --fixtures tests/fixtures/dj-workflow --render .omo/evidence/real-playable-juce/task-9-crossfade.wav --exit-after-init, --vst3-processing-smoke-test --chain deck-a --fixtures tests/fixtures/plugins --render .omo/evidence/real-playable-juce/task-10-deck-vst3.wav --exit-after-init, --vst3-editor-smoke-test --fixtures tests/fixtures/plugins --screenshot .omo/evidence/real-playable-juce/task-11-editor.png --exit-after-init, --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures tests/fixtures/plugins --exit-after-init, --playable-smoke-test --fixtures tests/fixtures/dj-workflow --screenshot .omo/evidence/real-playable-juce/task-13-playable.png --render .omo/evidence/real-playable-juce/task-13-mix.wav --exit-after-init, --performance-smoke-test --fixtures tests/fixtures/dj-workflow --sample-rate 48000 --buffer-size 512 --exit-after-init, --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --scope-audit --forbid \"Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor\" --paths src tests docs cmake, --browser-waveform-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --beatgrid-edit-smoke-test --exit-after-init, --juce-ui-smoke-test --dump-components --exit-after-init, or --juce-shell-smoke-test --exit-after-init for CI smoke validation.\n";
    }

    void shutdown() override { mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(DeckflaxiaApplication)
#else
namespace {

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--fixtures") {
            return std::filesystem::path(argv[index + 1]);
        }
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

std::optional<std::filesystem::path> pathAfterArgument(int argc, char* argv[], const char* flag) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == flag) {
            return std::filesystem::path(argv[index + 1]);
        }
    }
    return std::nullopt;
}


std::vector<std::filesystem::path> pathsAfterArgument(int argc, char* argv[], const char* flag) {
    std::vector<std::filesystem::path> paths;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) != flag) {
            continue;
        }
        for (int pathIndex = index + 1; pathIndex < argc && std::string(argv[pathIndex]).rfind("--", 0) != 0; ++pathIndex) {
            paths.emplace_back(argv[pathIndex]);
        }
        break;
    }
    return paths;
}


std::uint64_t unsignedAfterArgument(int argc, char* argv[], const char* flag) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == flag) {
            return static_cast<std::uint64_t>(std::strtoull(argv[index + 1], nullptr, 10));
        }
    }
    return 0U;
}

std::filesystem::path sandboxHelperPathFromExecutable(char* executablePath) {
    if (executablePath == nullptr) {
        return {};
    }
    return std::filesystem::path(executablePath).parent_path() / "DeckflaxiaPluginSandboxHelper";
}

std::vector<deckflaxia::library::FilesystemEntry> browserWaveformFixtures(const std::filesystem::path& fixtureDir) {
    return {{(fixtureDir / "track_120bpm.wav").string(), true},
            {(fixtureDir / "track_128bpm.wav").string(), true},
            {(fixtureDir / "track_95bpm.mp3").string(), true},
            {(fixtureDir / "corrupt_audio.wav").string(), true},
            {(fixtureDir / "not_audio.txt").string(), true}};
}

}

int main(int argc, char* argv[]) {
    const bool smokeTest = deckflaxia::app::hasArgument(argc, argv, "--smoke-test");
    const bool uiSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--ui-smoke-test");
    const bool alphaSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--alpha-smoke-test");
    const bool juceUiSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--juce-ui-smoke-test");
    const bool dumpComponents = deckflaxia::app::hasArgument(argc, argv, "--dump-components");
    const bool audioDeckSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--audio-deck-smoke-test");
    const bool tempoSyncSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--tempo-sync-smoke-test");
    const bool timeStretchOverloadSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--time-stretch-overload-smoke-test");
    const bool mixerSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--mixer-smoke-test");
    const bool vst3ProcessingSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--vst3-processing-smoke-test");
    const bool vst3EditorSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--vst3-editor-smoke-test");
    const bool pluginSandboxSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--plugin-sandbox-smoke-test");
    const bool playableSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--playable-smoke-test");
    const bool performanceSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--performance-smoke-test");
    const bool productionDjWorkflowSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--production-dj-workflow-smoke-test");
    const bool scopeAudit = deckflaxia::app::hasArgument(argc, argv, "--scope-audit");
    const bool browserWaveformSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--browser-waveform-smoke-test");
    const bool beatgridEditSmokeTest = deckflaxia::app::hasArgument(argc, argv, "--beatgrid-edit-smoke-test");
    const bool emptyLibrary = deckflaxia::app::hasArgument(argc, argv, "--empty-library");
    const bool exitAfterInit = deckflaxia::app::hasArgument(argc, argv, "--exit-after-init");
    const bool noAudioDevice = deckflaxia::app::hasArgument(argc, argv, "--no-audio-device");

    const auto screenshotPath = pathAfterArgument(argc, argv, "--screenshot");
    const auto renderPath = pathAfterArgument(argc, argv, "--render");
    const auto databasePath = pathAfterArgument(argc, argv, "--db");
    const auto chainName = pathAfterArgument(argc, argv, "--chain");
    const auto killHelperAfterMs = unsignedAfterArgument(argc, argv, "--kill-helper-after-ms");
    const auto sampleRateHz = static_cast<std::uint32_t>(unsignedAfterArgument(argc, argv, "--sample-rate"));
    const auto bufferFrames = static_cast<std::uint32_t>(unsignedAfterArgument(argc, argv, "--buffer-size"));
    const auto bootstrap = deckflaxia::app::initializeBootstrapServices(deckflaxia::app::BootstrapOptions{smokeTest || uiSmokeTest || alphaSmokeTest || juceUiSmokeTest || audioDeckSmokeTest || tempoSyncSmokeTest || timeStretchOverloadSmokeTest || mixerSmokeTest || vst3ProcessingSmokeTest || vst3EditorSmokeTest || pluginSandboxSmokeTest || playableSmokeTest || performanceSmokeTest || productionDjWorkflowSmokeTest || scopeAudit || browserWaveformSmokeTest || beatgridEditSmokeTest, noAudioDevice});
    std::cout << deckflaxia::app::formatBootstrapResult(bootstrap);

    if (!bootstrap.ok) {
        return 1;
    }

    if (uiSmokeTest && exitAfterInit) {
        return deckflaxia::app::runUiSmokeTest(emptyLibrary);
    }

    if (alphaSmokeTest && exitAfterInit) {
        return deckflaxia::app::runAlphaSmokeTest(std::cout);
    }

    if (audioDeckSmokeTest && exitAfterInit) {
        return deckflaxia::audio::runAudioDeckSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{fixtureDirectory(argc, argv)});
    }

    if (tempoSyncSmokeTest && exitAfterInit) {
        return deckflaxia::audio::runTempoSyncSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{fixtureDirectory(argc, argv)});
    }

    if (timeStretchOverloadSmokeTest && exitAfterInit) {
        return deckflaxia::audio::runTimeStretchOverloadSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{fixtureDirectory(argc, argv)});
    }

    if (mixerSmokeTest && exitAfterInit) {
        return deckflaxia::audio::runMixerSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{fixtureDirectory(argc, argv), renderPath.value_or(std::filesystem::path{})});
    }

    if (vst3ProcessingSmokeTest && exitAfterInit) {
        return deckflaxia::audio::runVst3ProcessingSmokeTest(std::cout, deckflaxia::audio::AudioDeckSmokeOptions{fixtureDirectory(argc, argv), renderPath.value_or(std::filesystem::path{}), chainName.has_value() ? chainName->string() : std::string{"deck-a"}});
    }

    if (vst3EditorSmokeTest && exitAfterInit) {
        return deckflaxia::ui::runVst3EditorSmokeTest(std::cout, deckflaxia::ui::VST3EditorSmokeOptions{fixtureDirectory(argc, argv), screenshotPath.value_or(std::filesystem::path{})});
    }

    if (pluginSandboxSmokeTest && exitAfterInit) {
        return deckflaxia::plugins::runPluginSandboxSmokeTest(std::cout, deckflaxia::plugins::PluginSandboxSmokeOptions{fixtureDirectory(argc, argv), sandboxHelperPathFromExecutable(argv[0]), killHelperAfterMs});
    }

    if (playableSmokeTest && exitAfterInit) {
        return deckflaxia::app::runPlayableSmokeTest(std::cout, deckflaxia::app::PlayableSmokeOptions{fixtureDirectory(argc, argv), screenshotPath.value_or(std::filesystem::path{}), renderPath.value_or(std::filesystem::path{}), databasePath.value_or(std::filesystem::path{}), sandboxHelperPathFromExecutable(argv[0]), deckflaxia::app::hasArgument(argc, argv, "--expect-restored-session")});
    }

    if (performanceSmokeTest && exitAfterInit) {
        return deckflaxia::app::runPerformanceSmokeTest(std::cout, deckflaxia::app::PerformanceSmokeOptions{fixtureDirectory(argc, argv), std::filesystem::path{".omo/evidence/real-playable-juce/task-15-performance.json"}, sampleRateHz == 0U ? 48000U : sampleRateHz, bufferFrames == 0U ? 512U : bufferFrames});
    }

    if (productionDjWorkflowSmokeTest && exitAfterInit) {
        return deckflaxia::app::runProductionDjWorkflowSmokeTest(std::cout, deckflaxia::app::ProductionDjWorkflowSmokeOptions{fixtureDirectory(argc, argv), std::filesystem::path{".omo/evidence/real-playable-juce/task-16-production-smoke.log"}, databasePath.value_or(std::filesystem::path{".omo/evidence/real-playable-juce/task-16-restart.db"}), sampleRateHz == 0U ? 48000U : sampleRateHz, bufferFrames == 0U ? 512U : bufferFrames});
    }

    if (scopeAudit) {
        const auto forbid = pathAfterArgument(argc, argv, "--forbid");
        auto paths = pathsAfterArgument(argc, argv, "--paths");
        return deckflaxia::app::runScopeAudit(std::cout, deckflaxia::app::ScopeAuditOptions{forbid.has_value() ? deckflaxia::app::splitScopeAuditTerms(forbid->string()) : std::vector<std::string>{}, paths.empty() ? std::vector<std::filesystem::path>{"src", "tests", "docs", "cmake"} : paths, std::filesystem::path{".omo/evidence/real-playable-juce/task-16-scope-audit.log"}});
    }

    if (browserWaveformSmokeTest && exitAfterInit) {
        return deckflaxia::ui::runBrowserWaveformSmokeTest(std::cout, browserWaveformFixtures(fixtureDirectory(argc, argv)));
    }

    if (beatgridEditSmokeTest && exitAfterInit) {
        return deckflaxia::ui::runBeatgridEditSmokeTest(std::cout);
    }

    if (juceUiSmokeTest && exitAfterInit) {
        return deckflaxia::ui::runUnavailableJuceUiSmoke(std::cout, dumpComponents, screenshotPath.value_or(std::filesystem::path{}));
    }

    if (smokeTest && exitAfterInit) {
        return 0;
    }

    std::cout << "Deckflaxia bootstrap executable ready. Pass --smoke-test --exit-after-init, --ui-smoke-test --exit-after-init, --alpha-smoke-test --exit-after-init, --audio-deck-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --tempo-sync-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --time-stretch-overload-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --mixer-smoke-test --fixtures tests/fixtures/dj-workflow --render .omo/evidence/real-playable-juce/task-9-crossfade.wav --exit-after-init, --vst3-processing-smoke-test --chain deck-a --fixtures tests/fixtures/plugins --render .omo/evidence/real-playable-juce/task-10-deck-vst3.wav --exit-after-init, --vst3-editor-smoke-test --fixtures tests/fixtures/plugins --screenshot .omo/evidence/real-playable-juce/task-11-editor.png --exit-after-init, --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures tests/fixtures/plugins --exit-after-init, --playable-smoke-test --fixtures tests/fixtures/dj-workflow --screenshot .omo/evidence/real-playable-juce/task-13-playable.png --render .omo/evidence/real-playable-juce/task-13-mix.wav --exit-after-init, --performance-smoke-test --fixtures tests/fixtures/dj-workflow --sample-rate 48000 --buffer-size 512 --exit-after-init, --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --scope-audit --forbid \"Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor\" --paths src tests docs cmake, --browser-waveform-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init, --beatgrid-edit-smoke-test --exit-after-init, or --juce-ui-smoke-test --dump-components --exit-after-init for guarded UI smoke validation.\n";
    return 0;
}
#endif
