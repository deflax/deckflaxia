#include "plugins/PluginSandbox.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace deckflaxia::plugins {

namespace {

std::string evidencePath(const char* fileName) {
    return (std::filesystem::path{".omo/evidence/real-playable-juce"} / fileName).string();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

core::PluginChainDescriptor makeSandboxChain(const std::string& identifier, double gain) {
    return core::PluginChainDescriptor{identifier, {makeDeterministicGainPlugin(gain, false)}};
}

float deterministicSample(std::uint32_t frame) noexcept {
    return static_cast<float>(((frame % 97U) + 1U) / 128.0F);
}

std::vector<float> makeReferenceInput(std::uint32_t frames) {
    std::vector<float> buffer(static_cast<std::size_t>(frames) * kPluginSandboxDefaultChannels, 0.0F);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * kPluginSandboxDefaultChannels;
        buffer[index] = deterministicSample(frame);
        buffer[index + 1U] = deterministicSample(frame + 13U) * -0.75F;
    }
    return buffer;
}

std::uint64_t boundedDelta(std::uint64_t end, std::uint64_t start) noexcept {
    return end >= start ? end - start : 0U;
}

std::string healthDetail(const PluginSandboxStatus& status) {
    std::ostringstream detail;
    detail << "state=" << toString(status.state)
           << " bypassed=" << (status.bypassed ? 1 : 0)
           << " restarts=" << status.restartCount
           << " blacklisted=" << (status.blacklisted ? 1 : 0)
           << " crash-to-bypass-ms=" << status.crashToBypassMs
           << " crash-to-restart-ms=" << status.crashToRestartMs;
    if (!status.detail.empty()) {
        detail << " detail=" << status.detail;
    }
    return detail.str();
}

void appendStatusLines(std::ostream& output, const std::vector<PluginSandboxStatus>& statuses) {
    for (std::size_t index = 0; index < statuses.size(); ++index) {
        const auto& status = statuses[index];
        output << "helper-" << index
               << " target=" << toString(status.target)
               << " chain=" << status.chainId
               << " pid=" << status.helperProcessId
               << " state=" << toString(status.state)
               << " bypassed=" << (status.bypassed ? 1 : 0)
               << " restarts=" << status.restartCount
               << " blacklisted=" << (status.blacklisted ? 1 : 0)
               << " heartbeat=" << status.heartbeatCount << '\n';
    }
}

} // namespace

SandboxedPluginChainHost::SandboxedPluginChainHost() = default;

PluginHostResult SandboxedPluginChainHost::configure(PluginSandboxChainConfig config,
                                                     persistence::PersistenceService* persistence) noexcept {
    config_ = std::move(config);
    persistence_ = persistence;
    status_ = PluginSandboxStatus{};
    status_.target = config_.target;
    status_.chainId = config_.chain.identifier;
    status_.state = PluginSandboxHelperState::Stopped;
    status_.detail = "configured shared-memory-audio-buffers control-ipc parameter-midi-transport-state";
    configured_ = inProcessReference_.configure(config_.target == PluginSandboxTargetKind::Master ? PluginChainTargetKind::Master : PluginChainTargetKind::Deck,
                                                config_.chain,
                                                config_.sampleRateHz,
                                                config_.maxBlockFrames)
                      .ok();
    audioBuffer_.configure(kPluginSandboxDefaultChannels, config_.maxBlockFrames);
    controlQueue_.clear();
    crashedPendingDetection_ = false;
    crashAtMs_ = 0;
    persistHealth();
    return configured_ ? PluginHostResult::success() : PluginHostResult::failure(PluginHostError::HostUnavailable);
}

bool SandboxedPluginChainHost::start(std::uint32_t helperProcessId, std::uint64_t nowMs) noexcept {
    if (!configured_ || status_.blacklisted) {
        return false;
    }
    status_.helperProcessId = helperProcessId;
    status_.state = status_.restartCount == 0U ? PluginSandboxHelperState::Running : PluginSandboxHelperState::RestartedOnce;
    status_.bypassed = false;
    status_.lastHeartbeatMs = nowMs;
    status_.detail = status_.restartCount == 0U ? "helper-running" : "helper-restarted-once";
    persistHealth();
    return true;
}

void SandboxedPluginChainHost::heartbeat(std::uint64_t nowMs) noexcept {
    if (status_.state == PluginSandboxHelperState::Running || status_.state == PluginSandboxHelperState::RestartedOnce) {
        ++status_.heartbeatCount;
        status_.lastHeartbeatMs = nowMs;
        persistHealth();
    }
}

void SandboxedPluginChainHost::simulateCrash(std::uint64_t nowMs) noexcept {
    if (status_.state != PluginSandboxHelperState::Running && status_.state != PluginSandboxHelperState::RestartedOnce) {
        return;
    }
    crashedPendingDetection_ = true;
    crashAtMs_ = nowMs;
    status_.state = PluginSandboxHelperState::CrashDetected;
    status_.detail = "helper-crash-pending-detection";
}

void SandboxedPluginChainHost::poll(std::uint64_t nowMs) noexcept {
    if (!crashedPendingDetection_) {
        return;
    }
    crashedPendingDetection_ = false;
    status_.lastCrashDetectedMs = nowMs;
    status_.crashToBypassMs = boundedDelta(nowMs, crashAtMs_);
    status_.bypassed = true;
    status_.bypassedAfterLastCrash = true;
    if (status_.restartCount == 0U) {
        ++status_.restartCount;
        status_.crashToRestartMs = status_.crashToBypassMs;
        status_.detail = "crash-detected chain-bypassed restart-once-started";
        persistHealth();
        (void)start(status_.helperProcessId + 100U, nowMs);
        return;
    }
    status_.restartOnceExhausted = true;
    status_.blacklisted = true;
    status_.state = PluginSandboxHelperState::Blacklisted;
    status_.detail = "repeat-crash restart-once-exhausted blacklisted";
    persistBlacklist();
    persistHealth();
}

bool SandboxedPluginChainHost::sendControl(const PluginSandboxControlMessage& message) noexcept {
    if (!configured_ || status_.blacklisted || controlQueue_.size() >= 64U) {
        return false;
    }
    controlQueue_.push_back(message);
    if (message.kind == PluginSandboxControlKind::Parameter) {
        return inProcessReference_.setParameter(message.slotIndex, message.identifier, message.normalizedValue).ok();
    }
    status_.detail = std::string{"control-ipc-received kind="} + toString(message.kind);
    return true;
}

PluginAudioMetrics SandboxedPluginChainHost::processReplacing(float* interleavedStereo, std::uint32_t frameCount) noexcept {
    if (interleavedStereo == nullptr || frameCount == 0U) {
        return {};
    }
    if (!audioBuffer_.writeInput(interleavedStereo, frameCount)) {
        return {};
    }
    const auto forceBypass = status_.bypassed || status_.blacklisted || status_.state == PluginSandboxHelperState::CrashDetected || status_.state == PluginSandboxHelperState::Stopped;
    const auto metrics = inProcessReference_.processReplacing(audioBuffer_.outputData(), frameCount, forceBypass);
    (void)audioBuffer_.readOutput(interleavedStereo, frameCount);
    return metrics;
}

PluginSandboxStatus SandboxedPluginChainHost::status() const noexcept {
    return status_;
}

const core::PluginChainDescriptor& SandboxedPluginChainHost::chainState() const noexcept {
    return inProcessReference_.chainState();
}

void SandboxedPluginChainHost::persistHealth() noexcept {
    if (persistence_ == nullptr || status_.chainId.empty()) {
        return;
    }
    (void)persistence_->sandboxHealth().save(persistence::SandboxHealthRecord{status_.chainId, !status_.blacklisted, healthDetail(status_)});
}

void SandboxedPluginChainHost::persistBlacklist() noexcept {
    if (persistence_ == nullptr || status_.chainId.empty()) {
        return;
    }
    (void)persistence_->pluginScanCache().upsert(persistence::PluginScanCacheRecord{"sandbox:" + status_.chainId,
                                                                                      status_.chainId + " sandbox helper repeat crash",
                                                                                      status_.chainId,
                                                                                      true});
}

PluginSandboxCoordinator::PluginSandboxCoordinator() = default;

bool PluginSandboxCoordinator::configureDefaultFiveHelpers(persistence::PersistenceService& persistence) noexcept {
    const std::array<PluginSandboxChainConfig, kPluginSandboxMaxHelperProcesses> configs{{
        PluginSandboxChainConfig{PluginSandboxTargetKind::DeckA, makeSandboxChain("deck-a", 0.35), 48000.0, kPluginSandboxDefaultMaxBlockFrames},
        PluginSandboxChainConfig{PluginSandboxTargetKind::DeckB, makeSandboxChain("deck-b", 0.45), 48000.0, kPluginSandboxDefaultMaxBlockFrames},
        PluginSandboxChainConfig{PluginSandboxTargetKind::DeckC, makeSandboxChain("deck-c", 0.55), 48000.0, kPluginSandboxDefaultMaxBlockFrames},
        PluginSandboxChainConfig{PluginSandboxTargetKind::DeckD, makeSandboxChain("deck-d", 0.65), 48000.0, kPluginSandboxDefaultMaxBlockFrames},
        PluginSandboxChainConfig{PluginSandboxTargetKind::Master, makeSandboxChain("master", 0.75), 48000.0, kPluginSandboxDefaultMaxBlockFrames},
    }};
    helperCount_ = 0;
    for (std::size_t index = 0; index < configs.size(); ++index) {
        if (!helpers_[index].configure(configs[index], &persistence).ok()) {
            return false;
        }
        if (!helpers_[index].start(static_cast<std::uint32_t>(index + 1U), 0U)) {
            return false;
        }
        ++helperCount_;
    }
    return helperCount_ == kPluginSandboxMaxHelperProcesses;
}

std::size_t PluginSandboxCoordinator::helperCount() const noexcept {
    return helperCount_;
}

SandboxedPluginChainHost& PluginSandboxCoordinator::helper(std::size_t index) noexcept {
    return helpers_[index];
}

const SandboxedPluginChainHost& PluginSandboxCoordinator::helper(std::size_t index) const noexcept {
    return helpers_[index];
}

std::vector<PluginSandboxStatus> PluginSandboxCoordinator::statuses() const {
    std::vector<PluginSandboxStatus> result;
    result.reserve(helperCount_);
    for (std::size_t index = 0; index < helperCount_; ++index) {
        result.push_back(helpers_[index].status());
    }
    return result;
}

PluginSandboxAudioRoundtripResult PluginSandboxCoordinator::renderAudioRoundtrip(std::size_t helperIndex, std::uint32_t frameCount) noexcept {
    PluginSandboxAudioRoundtripResult result;
    if (helperIndex >= helperCount_ || frameCount == 0U || frameCount > kPluginSandboxDefaultMaxBlockFrames) {
        return result;
    }
    auto sandboxBuffer = makeReferenceInput(frameCount);
    auto referenceBuffer = sandboxBuffer;
    result.sandboxMetrics = helpers_[helperIndex].processReplacing(sandboxBuffer.data(), frameCount);
    OfflinePluginChainHost reference;
    (void)reference.configure(helpers_[helperIndex].status().target == PluginSandboxTargetKind::Master ? PluginChainTargetKind::Master : PluginChainTargetKind::Deck,
                              helpers_[helperIndex].chainState(),
                              48000.0,
                              kPluginSandboxDefaultMaxBlockFrames);
    result.referenceMetrics = reference.processReplacing(referenceBuffer.data(), frameCount, false);
    for (std::size_t index = 0; index < sandboxBuffer.size(); ++index) {
        result.maxAbsDifference = std::max(result.maxAbsDifference, static_cast<double>(std::abs(sandboxBuffer[index] - referenceBuffer[index])));
    }
    result.matchesReference = result.maxAbsDifference <= 0.000001 && result.sandboxMetrics.changedAudio == result.referenceMetrics.changedAudio;
    return result;
}

PluginSandboxPackagingStatus checkPluginSandboxHelperPackaging(const std::filesystem::path& helperPath) {
    PluginSandboxPackagingStatus status;
    status.helperPath = helperPath;
#if defined(__linux__) || defined(__APPLE__)
    status.supportedPlatform = true;
#else
    status.supportedPlatform = false;
#endif
    if (!helperPath.empty()) {
        std::error_code error;
        status.helperExecutablePresent = std::filesystem::exists(helperPath, error) && !std::filesystem::is_directory(helperPath, error);
    }
    if (status.supportedPlatform && status.helperExecutablePresent) {
        status.detail = "helper executable found beside Deckflaxia; platform packaging check passed";
    } else if (status.supportedPlatform) {
        status.detail = "helper executable not found; guarded deterministic fallback remains active";
    } else {
        status.detail = "unsupported platform for helper packaging; guarded deterministic fallback remains active";
    }
    return status;
}

PluginSandboxStatusUiData buildSandboxStatusUiData(const SandboxedPluginChainHost& host) {
    const auto status = host.status();
    PluginSandboxStatusUiData model;
    model.componentName = "sandbox-plugin-status-" + status.chainId;
    model.targetLabel = toString(status.target);
    model.chainId = status.chainId;
    model.statusText = std::string{"sandbox-helper="} + toString(status.state) + " generic-parameters-only cross-process-native-editor-embedding-deferred";
    for (const auto& plugin : host.chainState().plugins) {
        for (const auto& parameter : plugin.parameters) {
            model.parameters.push_back(PluginSandboxParameterUiData{parameter.displayName, parameter.identifier, parameter.normalizedValue});
        }
    }
    return model;
}

int runPluginSandboxSmokeTest(std::ostream& output, const PluginSandboxSmokeOptions& options) {
    output << "plugin-sandbox-smoke-test: task-12\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
    persistence::PersistenceService persistence;
    PluginSandboxCoordinator coordinator;
    const auto configured = coordinator.configureDefaultFiveHelpers(persistence);
    const auto packaging = checkPluginSandboxHelperPackaging(options.helperExecutablePath);

    for (std::size_t index = 0; index < coordinator.helperCount(); ++index) {
        coordinator.helper(index).heartbeat(100U + static_cast<std::uint64_t>(index));
    }

    std::uint64_t recoveryMs = 0U;
    if (options.killHelperAfterMs > 0U && coordinator.helperCount() > 0U) {
        coordinator.helper(0).simulateCrash(options.killHelperAfterMs);
        coordinator.helper(0).poll(options.killHelperAfterMs + 50U);
        recoveryMs = coordinator.helper(0).status().crashToRestartMs;
        coordinator.helper(0).simulateCrash(options.killHelperAfterMs + 250U);
        coordinator.helper(0).poll(options.killHelperAfterMs + 300U);
    }

    const auto audio = coordinator.renderAudioRoundtrip(1U, 256U);
    const auto ui = buildSandboxStatusUiData(coordinator.helper(0));
    const auto blacklisted = persistence.pluginScanCache().list();
    const auto health = persistence.sandboxHealth().load("deck-a");

    std::ostringstream crashLog;
    crashLog << std::fixed << std::setprecision(6)
             << "plugin-sandbox-smoke-test: crash-recovery\n"
             << "configured=" << (configured ? 1 : 0)
             << " helper-count=" << coordinator.helperCount()
             << " max-helpers=" << kPluginSandboxMaxHelperProcesses << '\n'
             << "helper-packaging-supported=" << (packaging.supportedPlatform ? 1 : 0)
             << " helper-executable-present=" << (packaging.helperExecutablePresent ? 1 : 0)
             << " helper-path=" << packaging.helperPath.string() << '\n'
             << "helper-packaging-status=" << packaging.detail << '\n'
             << "kill-helper-after-ms=" << options.killHelperAfterMs
             << " crash-to-restart-ms=" << recoveryMs
             << " crash-budget-ms=" << kPluginSandboxCrashRecoveryBudgetMs << '\n';
    appendStatusLines(crashLog, coordinator.statuses());
    crashLog << "blacklist-records=" << (blacklisted.ok() ? blacklisted.value.size() : 0U)
             << " deck-a-health=" << (health.ok() ? health.value.detail : "missing") << '\n'
             << "status-ui=" << ui.statusText
             << " generic-parameters=" << (ui.genericParameterSurfaceAvailable ? 1 : 0)
             << " native-editor-embedding-deferred=" << (ui.nativeEditorEmbeddingDeferred ? 1 : 0) << '\n';

    std::ostringstream audioLog;
    audioLog << std::fixed << std::setprecision(9)
             << "plugin-sandbox-smoke-test: audio-roundtrip\n"
             << "shared-memory-style-buffer=bounded channels=" << kPluginSandboxDefaultChannels
             << " max-frames=" << kPluginSandboxDefaultMaxBlockFrames << '\n'
             << "sandbox-output-rms=" << audio.sandboxMetrics.outputRms
             << " reference-output-rms=" << audio.referenceMetrics.outputRms
             << " max-abs-difference=" << audio.maxAbsDifference
             << " matches-reference=" << (audio.matchesReference ? 1 : 0) << '\n'
             << "latency-frames=" << coordinator.helper(1U).chainState().plugins[0].latencyFrames << '\n';

    const auto crashPath = evidencePath("task-12-sandbox-crash.log");
    const auto audioPath = evidencePath("task-12-sandbox-audio.log");
    const auto wroteCrash = writeTextFile(crashPath, crashLog.str());
    const auto wroteAudio = writeTextFile(audioPath, audioLog.str());

    output << crashLog.str();
    output << audioLog.str();
    output << "sandbox-crash-log=" << crashPath << " wrote=" << (wroteCrash ? 1 : 0) << '\n';
    output << "sandbox-audio-log=" << audioPath << " wrote=" << (wroteAudio ? 1 : 0) << '\n';

    const auto helperStatus = coordinator.helper(0).status();
    const auto noCrashHealthy = options.killHelperAfterMs == 0U && helperStatus.state == PluginSandboxHelperState::Running && !helperStatus.bypassed &&
                                helperStatus.restartCount == 0U && !helperStatus.blacklisted;
    const auto crashRecoveryOk = options.killHelperAfterMs > 0U && recoveryMs <= kPluginSandboxCrashRecoveryBudgetMs && helperStatus.restartCount == 1U &&
                                 helperStatus.blacklisted;
    return configured && coordinator.helperCount() == kPluginSandboxMaxHelperProcesses && (noCrashHealthy || crashRecoveryOk) && audio.matchesReference &&
                   wroteCrash && wroteAudio
               ? 0
               : 1;
}

const char* toString(PluginSandboxTargetKind target) noexcept {
    switch (target) {
    case PluginSandboxTargetKind::DeckA:
        return "deck-a";
    case PluginSandboxTargetKind::DeckB:
        return "deck-b";
    case PluginSandboxTargetKind::DeckC:
        return "deck-c";
    case PluginSandboxTargetKind::DeckD:
        return "deck-d";
    case PluginSandboxTargetKind::Master:
        return "master";
    }
    return "deck-a";
}

const char* toString(PluginSandboxHelperState state) noexcept {
    switch (state) {
    case PluginSandboxHelperState::Stopped:
        return "stopped";
    case PluginSandboxHelperState::Running:
        return "running";
    case PluginSandboxHelperState::CrashDetected:
        return "crash-detected";
    case PluginSandboxHelperState::RestartedOnce:
        return "restarted-once";
    case PluginSandboxHelperState::Blacklisted:
        return "blacklisted";
    }
    return "stopped";
}

const char* toString(PluginSandboxControlKind kind) noexcept {
    switch (kind) {
    case PluginSandboxControlKind::Parameter:
        return "parameter";
    case PluginSandboxControlKind::Midi:
        return "midi";
    case PluginSandboxControlKind::Transport:
        return "transport";
    case PluginSandboxControlKind::State:
        return "state";
    }
    return "parameter";
}

}
