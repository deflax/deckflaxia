#pragma once

#include "core/DomainModels.h"
#include "ipc/BoundedAudioBuffer.h"
#include "persistence/Persistence.h"
#include "plugins/PluginChainProcessor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace deckflaxia::plugins {

constexpr std::size_t kPluginSandboxMaxHelperProcesses = 5;
constexpr std::uint32_t kPluginSandboxDefaultMaxBlockFrames = 512;
constexpr std::uint32_t kPluginSandboxDefaultChannels = 2;
constexpr std::uint64_t kPluginSandboxCrashRecoveryBudgetMs = 2000;

enum class PluginSandboxTargetKind : std::uint8_t {
    DeckA,
    DeckB,
    DeckC,
    DeckD,
    Master,
};

enum class PluginSandboxHelperState : std::uint8_t {
    Stopped,
    Running,
    CrashDetected,
    RestartedOnce,
    Blacklisted,
};

enum class PluginSandboxControlKind : std::uint8_t {
    Parameter,
    Midi,
    Transport,
    State,
};

struct PluginSandboxControlMessage final {
    PluginSandboxControlKind kind{PluginSandboxControlKind::Parameter};
    std::size_t slotIndex{};
    std::string identifier;
    double normalizedValue{};
    std::uint64_t sampleTime{};
};

struct PluginSandboxChainConfig final {
    PluginSandboxTargetKind target{PluginSandboxTargetKind::DeckA};
    core::PluginChainDescriptor chain;
    double sampleRateHz{48000.0};
    std::uint32_t maxBlockFrames{kPluginSandboxDefaultMaxBlockFrames};
};

struct PluginSandboxStatus final {
    PluginSandboxTargetKind target{PluginSandboxTargetKind::DeckA};
    PluginSandboxHelperState state{PluginSandboxHelperState::Stopped};
    std::string chainId;
    std::uint32_t helperProcessId{};
    std::uint32_t restartCount{};
    std::uint32_t heartbeatCount{};
    bool bypassed{};
    bool blacklisted{};
    bool restartOnceExhausted{};
    bool bypassedAfterLastCrash{};
    std::uint64_t lastHeartbeatMs{};
    std::uint64_t lastCrashDetectedMs{};
    std::uint64_t crashToBypassMs{};
    std::uint64_t crashToRestartMs{};
    std::string detail;
};

struct PluginSandboxPackagingStatus final {
    bool supportedPlatform{};
    bool helperExecutablePresent{};
    std::filesystem::path helperPath;
    std::string detail;
};

struct PluginSandboxParameterUiData final {
    std::string displayName;
    std::string identifier;
    double normalizedValue{};
};

struct PluginSandboxStatusUiData final {
    std::string componentName;
    std::string targetLabel;
    std::string chainId;
    std::string statusText;
    bool genericParameterSurfaceAvailable{true};
    bool nativeEditorEmbeddingDeferred{true};
    std::vector<PluginSandboxParameterUiData> parameters;
};

struct PluginSandboxAudioRoundtripResult final {
    PluginAudioMetrics sandboxMetrics{};
    PluginAudioMetrics referenceMetrics{};
    double maxAbsDifference{};
    bool matchesReference{};
};

class SandboxedPluginChainHost final {
public:
    SandboxedPluginChainHost();

    [[nodiscard]] PluginHostResult configure(PluginSandboxChainConfig config,
                                             persistence::PersistenceService* persistence) noexcept;
    [[nodiscard]] bool start(std::uint32_t helperProcessId, std::uint64_t nowMs) noexcept;
    void heartbeat(std::uint64_t nowMs) noexcept;
    void simulateCrash(std::uint64_t nowMs) noexcept;
    void poll(std::uint64_t nowMs) noexcept;
    [[nodiscard]] bool sendControl(const PluginSandboxControlMessage& message) noexcept;
    [[nodiscard]] PluginAudioMetrics processReplacing(float* interleavedStereo, std::uint32_t frameCount) noexcept;
    [[nodiscard]] PluginSandboxStatus status() const noexcept;
    [[nodiscard]] const core::PluginChainDescriptor& chainState() const noexcept;

private:
    void persistHealth() noexcept;
    void persistBlacklist() noexcept;

    PluginSandboxChainConfig config_{};
    OfflinePluginChainHost inProcessReference_{};
    ipc::BoundedAudioBuffer audioBuffer_{kPluginSandboxDefaultChannels, kPluginSandboxDefaultMaxBlockFrames};
    persistence::PersistenceService* persistence_{};
    PluginSandboxStatus status_{};
    bool configured_{};
    bool crashedPendingDetection_{};
    std::uint64_t crashAtMs_{};
    std::vector<PluginSandboxControlMessage> controlQueue_{};
};

class PluginSandboxCoordinator final {
public:
    PluginSandboxCoordinator();

    [[nodiscard]] bool configureDefaultFiveHelpers(persistence::PersistenceService& persistence) noexcept;
    [[nodiscard]] std::size_t helperCount() const noexcept;
    [[nodiscard]] SandboxedPluginChainHost& helper(std::size_t index) noexcept;
    [[nodiscard]] const SandboxedPluginChainHost& helper(std::size_t index) const noexcept;
    [[nodiscard]] std::vector<PluginSandboxStatus> statuses() const;
    [[nodiscard]] PluginSandboxAudioRoundtripResult renderAudioRoundtrip(std::size_t helperIndex, std::uint32_t frameCount) noexcept;

private:
    std::array<SandboxedPluginChainHost, kPluginSandboxMaxHelperProcesses> helpers_{};
    std::size_t helperCount_{};
};

struct PluginSandboxSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/plugins"};
    std::filesystem::path helperExecutablePath{};
    std::uint64_t killHelperAfterMs{};
};

[[nodiscard]] PluginSandboxPackagingStatus checkPluginSandboxHelperPackaging(const std::filesystem::path& helperPath);
[[nodiscard]] PluginSandboxStatusUiData buildSandboxStatusUiData(const SandboxedPluginChainHost& host);
[[nodiscard]] int runPluginSandboxSmokeTest(std::ostream& output, const PluginSandboxSmokeOptions& options);
[[nodiscard]] const char* toString(PluginSandboxTargetKind target) noexcept;
[[nodiscard]] const char* toString(PluginSandboxHelperState state) noexcept;
[[nodiscard]] const char* toString(PluginSandboxControlKind kind) noexcept;

}
