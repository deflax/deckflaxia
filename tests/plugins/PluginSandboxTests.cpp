#include "plugins/PluginSandbox.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

template <typename T>
int expectOk(const T& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int testHelperLimitAndStatusUi() {
    djapp::persistence::PersistenceService persistence;
    djapp::plugins::PluginSandboxCoordinator coordinator;
    if (expect(coordinator.configureDefaultFiveHelpers(persistence), "default sandbox helpers should configure") != 0) {
        return 1;
    }
    if (expect(coordinator.helperCount() == djapp::plugins::kPluginSandboxMaxHelperProcesses, "sandbox should expose exactly five helper chains") != 0) {
        return 1;
    }
    const auto ui = djapp::plugins::buildSandboxStatusUiData(coordinator.helper(0));
    if (expect(ui.genericParameterSurfaceAvailable && ui.nativeEditorEmbeddingDeferred, "sandbox UI should be generic/status only") != 0) {
        return 1;
    }
    if (expect(!ui.parameters.empty() && ui.statusText.find("cross-process-native-editor-embedding-deferred") != std::string::npos,
               "sandbox UI should expose parameters and deferred native editor status") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.HelperLimit helpers=" << coordinator.helperCount() << " ui-parameters=" << ui.parameters.size() << '\n';
    return 0;
}

int testCrashRestartBlacklist() {
    djapp::persistence::PersistenceService persistence;
    djapp::plugins::PluginSandboxCoordinator coordinator;
    if (expect(coordinator.configureDefaultFiveHelpers(persistence), "default sandbox helpers should configure") != 0) {
        return 1;
    }
    auto& helper = coordinator.helper(0);
    helper.simulateCrash(500);
    helper.poll(550);
    auto status = helper.status();
    if (expect(status.restartCount == 1U && status.bypassedAfterLastCrash && !status.blacklisted,
               "first crash should bypass then restart once") != 0) {
        return 1;
    }
    if (expect(status.crashToRestartMs <= djapp::plugins::kPluginSandboxCrashRecoveryBudgetMs,
               "first crash restart should meet recovery budget") != 0) {
        return 1;
    }
    helper.simulateCrash(800);
    helper.poll(850);
    status = helper.status();
    if (expect(status.blacklisted && status.restartOnceExhausted && status.bypassed,
               "repeat crash should blacklist and leave chain bypassed") != 0) {
        return 1;
    }
    const auto cache = persistence.pluginScanCache().list();
    if (expectOk(cache, "plugin blacklist cache list") != 0 || expect(!cache.value.empty() && cache.value[0].blacklisted, "sandbox blacklist should use plugin scan cache") != 0) {
        return 1;
    }
    const auto health = persistence.sandboxHealth().load("deck-a");
    if (expectOk(health, "sandbox health load") != 0 || expect(!health.value.healthy && health.value.detail.find("blacklisted") != std::string::npos, "sandbox health should persist repeat crash") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.CrashRestartBlacklist recovery-ms=" << status.crashToRestartMs << " blacklisted=" << status.blacklisted << '\n';
    return 0;
}

int testControlIpcSurface() {
    djapp::persistence::PersistenceService persistence;
    djapp::plugins::SandboxedPluginChainHost host;
    const djapp::core::PluginChainDescriptor chain{"deck-a", {djapp::plugins::makeDeterministicGainPlugin(0.5, false)}};
    if (expectOk(host.configure(djapp::plugins::PluginSandboxChainConfig{djapp::plugins::PluginSandboxTargetKind::DeckA, chain, 48000.0, 512}, &persistence), "host configure") != 0) {
        return 1;
    }
    if (expect(host.start(1, 0), "host start") != 0) {
        return 1;
    }
    if (expect(host.sendControl(djapp::plugins::PluginSandboxControlMessage{djapp::plugins::PluginSandboxControlKind::Parameter, 0, "gain", 0.25, 0}), "parameter control should send") != 0) {
        return 1;
    }
    if (expect(host.sendControl(djapp::plugins::PluginSandboxControlMessage{djapp::plugins::PluginSandboxControlKind::Midi, 0, "cc-10", 1.0, 32}), "midi control should send") != 0) {
        return 1;
    }
    if (expect(host.sendControl(djapp::plugins::PluginSandboxControlMessage{djapp::plugins::PluginSandboxControlKind::Transport, 0, "play", 1.0, 64}), "transport control should send") != 0) {
        return 1;
    }
    if (expect(host.sendControl(djapp::plugins::PluginSandboxControlMessage{djapp::plugins::PluginSandboxControlKind::State, 0, "state-bytes", 1.0, 96}), "state control should send") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.ControlIpc parameter-midi-transport-state=1\n";
    return 0;
}

int testAudioRoundtrip() {
    djapp::persistence::PersistenceService persistence;
    djapp::plugins::PluginSandboxCoordinator coordinator;
    if (expect(coordinator.configureDefaultFiveHelpers(persistence), "default sandbox helpers should configure") != 0) {
        return 1;
    }
    const auto result = coordinator.renderAudioRoundtrip(0, 256);
    if (expect(result.matchesReference, "sandbox output should match in-process reference") != 0) {
        return 1;
    }
    if (expect(result.sandboxMetrics.changedAudio, "sandbox plugin should change audio") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.AudioRoundtrip max-abs-difference=" << result.maxAbsDifference
              << " output-rms=" << result.sandboxMetrics.outputRms << '\n';
    return 0;
}

int testSmokeSurfaceNoKill(const std::filesystem::path& fixtures, const std::filesystem::path& helperPath) {
    std::ostringstream output;
    const auto result = djapp::plugins::runPluginSandboxSmokeTest(output, djapp::plugins::PluginSandboxSmokeOptions{fixtures, helperPath, 0});
    const auto text = output.str();
    if (expect(result == 0, "plugin sandbox no-kill smoke should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("helper-count=5") != std::string::npos && text.find("matches-reference=1") != std::string::npos,
               "smoke should report helper cap and audio reference match") != 0) {
        return 1;
    }
    if (expect(text.find("kill-helper-after-ms=0") != std::string::npos && text.find("blacklist-records=0") != std::string::npos &&
                   text.find("helper-0 target=deck-a chain=deck-a pid=1 state=running bypassed=0 restarts=0 blacklisted=0") != std::string::npos,
               "no-kill smoke should report healthy running helpers without blacklist") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.SmokeSurfaceNoKill ok\n";
    return 0;
}

int testSmokeSurfaceCrash(const std::filesystem::path& fixtures, const std::filesystem::path& helperPath) {
    std::ostringstream output;
    const auto result = djapp::plugins::runPluginSandboxSmokeTest(output, djapp::plugins::PluginSandboxSmokeOptions{fixtures, helperPath, 500});
    const auto text = output.str();
    if (expect(result == 0, "plugin sandbox crash smoke should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("helper-count=5") != std::string::npos && text.find("matches-reference=1") != std::string::npos,
               "crash smoke should report helper cap and audio reference match") != 0) {
        return 1;
    }
    if (expect(text.find("blacklisted=1") != std::string::npos && text.find("crash-to-restart-ms=50") != std::string::npos,
               "smoke should report restart and blacklist semantics") != 0) {
        return 1;
    }
    std::cout << "PluginSandbox.SmokeSurfaceCrash ok\n";
    return 0;
}

int testSmokeSurface(const std::filesystem::path& fixtures, const std::filesystem::path& helperPath) {
    return testSmokeSurfaceNoKill(fixtures, helperPath) != 0 || testSmokeSurfaceCrash(fixtures, helperPath) != 0 ? 1 : 0;
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    return argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures/plugins");
}

std::filesystem::path helperPath(int argc, char* argv[]) {
    return argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{};
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);
    const auto helper = helperPath(argc, argv);
    if (filter == "limit") {
        return testHelperLimitAndStatusUi();
    }
    if (filter == "crash") {
        return testCrashRestartBlacklist();
    }
    if (filter == "control") {
        return testControlIpcSurface();
    }
    if (filter == "audio") {
        return testAudioRoundtrip();
    }
    if (filter == "smoke") {
        return testSmokeSurface(fixtures, helper);
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown PluginSandbox filter " << filter << '\n';
        return 1;
    }
    if (testHelperLimitAndStatusUi() != 0 || testCrashRestartBlacklist() != 0 || testControlIpcSurface() != 0 || testAudioRoundtrip() != 0 || testSmokeSurface(fixtures, helper) != 0) {
        return 1;
    }
    std::cout << "Plugin sandbox tests passed\n";
    return 0;
}
