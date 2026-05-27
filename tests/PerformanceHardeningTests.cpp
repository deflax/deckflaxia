#include "app/PlayableSmoke.h"
#include "audio/AudioEngine.h"
#include "decks/FourDeckPlaybackCore.h"
#include "library/AudioImport.h"
#include "persistence/Persistence.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginSandbox.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

template <typename Result>
int expectOk(const Result& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

deckflaxia::core::BackgroundJobTicket databaseTicket(std::uint64_t id) {
    return deckflaxia::core::BackgroundJobTicket{id, deckflaxia::core::BackgroundJobKind::PersistLibraryChange, deckflaxia::core::BackgroundWorkerRole::DatabaseWorker};
}

deckflaxia::core::BackgroundJobTicket invalidPluginScanTicket() {
    return deckflaxia::core::BackgroundJobTicket{1504, deckflaxia::core::BackgroundJobKind::PersistLibraryChange, deckflaxia::core::BackgroundWorkerRole::DatabaseWorker};
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return static_cast<bool>(file);
}

int loadFixtureDeck(deckflaxia::decks::FourDeckPlaybackCore& core, const std::filesystem::path& path) {
    deckflaxia::decks::PreparedAudioMedia media;
    const auto fileLoad = deckflaxia::decks::loadPcm16WavFileToPreparedMedia(path, media);
    if (expectOk(fileLoad, "fixture decode") != 0) {
        return 1;
    }
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    if (expectOk(core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::preparedAudio(std::move(media))), "deck load") != 0) {
        return 1;
    }
    if (expect(core.syncTempo(deckId, 120.0, 128.0, true) == deckflaxia::decks::FourDeckPlaybackError::None, "tempo sync should configure") != 0) {
        return 1;
    }
    return expect(core.play(deckId) == deckflaxia::decks::FourDeckPlaybackError::None, "deck should play");
}

int testBudgets(const std::filesystem::path& fixtures) {
    const std::filesystem::path evidencePath{".omo/evidence/real-playable-juce/task-15-performance.json"};
    std::ostringstream output;
    const auto result = deckflaxia::app::runPerformanceSmokeTest(output, deckflaxia::app::PerformanceSmokeOptions{fixtures, evidencePath, 48000, 512});
    const auto text = output.str();
    if (expect(result == 0, "performance smoke should pass budgets") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("callback-pass=1") != std::string::npos &&
                   text.find("ui-pass=1") != std::string::npos &&
                   text.find("sandbox-pass=1") != std::string::npos &&
                   text.find("underrun-pass=1") != std::string::npos,
               "performance smoke should print budget pass fields") != 0) {
        std::cerr << text;
        return 1;
    }
    const auto json = readTextFile(evidencePath);
    if (expect(json.find("\"passed\": true") != std::string::npos && json.find("\"decodeQueueDepth\"") != std::string::npos,
               "performance JSON should record passing budgets and decode pressure") != 0) {
        return 1;
    }
    std::cout << "PerformanceHardening.Budgets ok evidence=" << evidencePath << '\n';
    return 0;
}

int testFaults(const std::filesystem::path& fixtures) {
    std::ostringstream log;
    bool ok = true;

    auto deviceService = deckflaxia::audio::AudioDeviceService(deckflaxia::audio::backendPolicyForPlatform(deckflaxia::audio::HostAudioPlatform::Linux));
    deviceService.markMissingDevice(deckflaxia::audio::AudioRenderConfiguration{48000, 512});
    const auto audioDeviceOk = deviceService.state().status == deckflaxia::audio::AudioDeviceConnectionStatus::MissingDevice && deviceService.state().degraded;
    ok = ok && audioDeviceOk;
    log << "audio-device-unavailable typed-status=" << deckflaxia::audio::toString(deviceService.state().status)
        << " degraded=" << (deviceService.state().degraded ? 1 : 0) << '\n';

    deckflaxia::decks::PreparedAudioMedia missingMedia;
    const auto missing = deckflaxia::decks::loadPcm16WavFileToPreparedMedia(fixtures / "missing-task-15.wav", missingMedia);
    const auto missingOk = !missing.ok() && missing.error == deckflaxia::decks::AudioDeckLoadError::MissingMedia;
    ok = ok && missingOk;
    log << "missing-file error=" << deckflaxia::decks::toString(missing.error) << '\n';

    deckflaxia::decks::PreparedAudioMedia corruptMedia;
    const auto corrupt = deckflaxia::decks::loadPcm16WavFileToPreparedMedia(fixtures / "corrupt_audio.wav", corruptMedia);
    const auto corruptOk = !corrupt.ok() && corrupt.error == deckflaxia::decks::AudioDeckLoadError::CorruptMedia;
    ok = ok && corruptOk;
    log << "corrupt-file error=" << deckflaxia::decks::toString(corrupt.error) << '\n';

    deckflaxia::persistence::PersistenceService pluginPersistence;
    deckflaxia::plugins::Vst3PluginManager pluginManager(pluginPersistence.pluginScanCache());
    deckflaxia::plugins::PluginScanWorkerModel pluginWorker;
    const auto scan = pluginManager.scanOnBackgroundWorker(deckflaxia::plugins::PluginScanDescriptor{1504, {deckflaxia::plugins::PluginScanCandidate{"Bad", "/plugins/bad.component"}}}, invalidPluginScanTicket(), pluginWorker);
    const auto scanOk = !scan.ok() && scan.error == deckflaxia::plugins::PluginScanError::WorkerUnavailable;
    ok = ok && scanOk;
    log << "plugin-scan-failure typed-error=" << static_cast<int>(scan.error) << '\n';

    deckflaxia::persistence::PersistenceService sandboxPersistence;
    deckflaxia::plugins::PluginSandboxCoordinator sandbox;
    const auto sandboxConfigured = sandbox.configureDefaultFiveHelpers(sandboxPersistence);
    if (sandboxConfigured) {
        sandbox.helper(0).simulateCrash(500);
        sandbox.helper(0).poll(550);
    }
    const auto sandboxStatus = sandboxConfigured ? sandbox.helper(0).status() : deckflaxia::plugins::PluginSandboxStatus{};
    const auto sandboxOk = sandboxConfigured && sandboxStatus.restartCount == 1U && sandboxStatus.crashToRestartMs <= deckflaxia::plugins::kPluginSandboxCrashRecoveryBudgetMs;
    ok = ok && sandboxOk;
    log << "plugin-process-crash restart-count=" << sandboxStatus.restartCount
        << " crash-to-restart-ms=" << sandboxStatus.crashToRestartMs
        << " budget-ms=" << deckflaxia::plugins::kPluginSandboxCrashRecoveryBudgetMs << '\n';

    deckflaxia::persistence::PersistenceService lockedPersistence;
    lockedPersistence.store().setLockedForTest(true);
    const auto locked = lockedPersistence.migrateOnDatabaseWorker(databaseTicket(1505));
    const auto lockedOk = !locked.ok() && locked.error == deckflaxia::persistence::PersistenceError::DatabaseLocked && deckflaxia::persistence::isRecoverable(locked.error);
    ok = ok && lockedOk;
    log << "sqlite-locked-or-local-fallback typed-error=" << static_cast<int>(locked.error)
        << " recoverable=" << (deckflaxia::persistence::isRecoverable(locked.error) ? 1 : 0) << '\n';

    deckflaxia::decks::FourDeckPlaybackCore stretchCore;
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    const auto stretchLoaded = loadFixtureDeck(stretchCore, fixtures / "track_120bpm.wav") == 0;
    const auto bypassConfigured = stretchLoaded && stretchCore.setTimeStretchBypass(deckId, true) == deckflaxia::decks::FourDeckPlaybackError::None;
    const auto render = stretchCore.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 2);
    const auto stretchStatus = stretchCore.deck(deckId).timeStretchStatus();
    const auto stretchOk = bypassConfigured && render.ok() && stretchStatus.bypassed && stretchStatus.fallbackEvents > 0U;
    ok = ok && stretchOk;
    log << "stretch-overload bypassed=" << (stretchStatus.bypassed ? 1 : 0)
        << " fallback-events=" << stretchStatus.fallbackEvents
        << " callback-max-ms=" << render.metrics.maxCallbackMs << '\n';

    log << "performance-hardening-faults: " << (ok ? "ok" : "fail") << '\n';
    const std::filesystem::path faultPath{".omo/evidence/real-playable-juce/task-15-faults.log"};
    const auto wrote = writeTextFile(faultPath, log.str());
    if (expect(wrote, "fault evidence should write") != 0) {
        return 1;
    }
    if (!ok) {
        std::cerr << log.str();
        return 1;
    }
    std::cout << log.str();
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const std::filesystem::path fixtures = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures/dj-workflow");

    if (filter == "budgets") {
        return testBudgets(fixtures);
    }
    if (filter == "faults") {
        return testFaults(fixtures);
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown PerformanceHardening filter " << filter << '\n';
        return 1;
    }
    if (testBudgets(fixtures) != 0 || testFaults(fixtures) != 0) {
        return 1;
    }
    std::cout << "PerformanceHardening tests passed\n";
    return 0;
}
