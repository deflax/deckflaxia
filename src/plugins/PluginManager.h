#pragma once

#include "audio/routing/AudioRoutingGraph.h"
#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"
#include "persistence/Persistence.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deckflaxia::plugins {

constexpr const char* kPluginHostAlphaScope = "VST3-only in-process alpha host using deterministic placeholders when native hosting is unavailable.";
constexpr const char* kPluginHostInProcessLimitation = "Plugin failures are not process-isolated in alpha; scan and graph changes stay off the audio callback path.";

enum class PluginFormatKind : std::uint8_t {
    VST3,
};

enum class PluginScanError : std::uint8_t {
    None,
    WorkerUnavailable,
    RepositoryFailure,
    InvalidCandidate,
};

enum class PluginSlotRecoveryState : std::uint8_t {
    Available,
    MissingPluginPlaceholder,
};

struct PluginDescriptor final {
    std::string pluginId;
    std::string displayName;
    std::string path;
    PluginFormatKind format{PluginFormatKind::VST3};
};

struct PluginScanCandidate final {
    std::string displayName;
    std::string path;
};

struct PluginScanDescriptor final {
    std::uint64_t scanId{};
    std::vector<PluginScanCandidate> candidates;
};

struct PluginScanResult final {
    std::vector<PluginDescriptor> discovered;
    std::vector<std::string> blacklistedPaths;
    bool cancelled{};
    PluginScanError error{PluginScanError::None};

    [[nodiscard]] bool ok() const noexcept { return error == PluginScanError::None; }
};

struct PluginRecoverySlot final {
    core::PluginDescriptor savedPlugin;
    PluginSlotRecoveryState state{PluginSlotRecoveryState::MissingPluginPlaceholder};
};

struct PluginChainRecovery final {
    std::string chainId;
    std::vector<PluginRecoverySlot> slots;
};

class AudioPluginFormatManager final {
public:
    AudioPluginFormatManager() noexcept;

    [[nodiscard]] std::size_t formatCount() const noexcept;
    [[nodiscard]] bool supportsFormat(PluginFormatKind format) const noexcept;
    [[nodiscard]] bool onlyVst3Enabled() const noexcept;

private:
    bool vst3Registered_{true};
};

class KnownPluginList final {
public:
    [[nodiscard]] PluginScanError loadFromRepository(const persistence::PluginScanCacheRepository& repository);
    [[nodiscard]] PluginScanError addOrUpdate(const PluginDescriptor& descriptor, persistence::PluginScanCacheRepository& repository);
    [[nodiscard]] PluginScanError blacklistPath(const std::string& path, persistence::PluginScanCacheRepository& repository);

    [[nodiscard]] bool contains(const std::string& pluginId) const noexcept;
    [[nodiscard]] bool isPathBlacklisted(const std::string& path) const noexcept;
    [[nodiscard]] const std::vector<PluginDescriptor>& availablePlugins() const noexcept;

private:
    std::vector<PluginDescriptor> available_;
    std::vector<std::string> blacklistedPaths_;
};

class PluginScanWorkerModel final : public core::CancellableBackgroundWorker {
public:
    [[nodiscard]] bool trySchedule(core::BackgroundJobTicket ticket) noexcept override;
    void requestStop() noexcept override;
    [[nodiscard]] bool stopRequested() const noexcept override;
    [[nodiscard]] bool scheduled() const noexcept;
    void requestStopAfterCandidateCount(std::size_t candidateCount) noexcept;
    [[nodiscard]] bool shouldStopAfterCandidate(std::size_t processedCandidateCount) const noexcept;

private:
    bool scheduled_{};
    bool stopRequested_{};
    std::size_t stopAfterCandidateCount_{};
};

class PluginDirectoryScanner final {
public:
    explicit PluginDirectoryScanner(persistence::PluginScanCacheRepository repository);

    [[nodiscard]] PluginScanResult scan(const PluginScanDescriptor& descriptor,
                                        core::BackgroundJobTicket ticket,
                                        PluginScanWorkerModel& worker,
                                        KnownPluginList& knownPlugins);

private:
    persistence::PluginScanCacheRepository repository_;
};

class PluginGraphCommandModel final {
public:
    [[nodiscard]] audio::routing::RoutingGraphResult insertManualPlugin(audio::routing::AudioRoutingGraphController& graph,
                                                                        core::DeckId deckId,
                                                                        std::size_t slotIndex,
                                                                        PluginSlotRecoveryState state) const noexcept;
    [[nodiscard]] audio::routing::RoutingGraphResult removeManualPlugin(audio::routing::AudioRoutingGraphController& graph,
                                                                        core::DeckId deckId,
                                                                        std::size_t slotIndex) const noexcept;
};

class Vst3PluginManager final {
public:
    explicit Vst3PluginManager(persistence::PluginScanCacheRepository repository);

    [[nodiscard]] const AudioPluginFormatManager& formatManager() const noexcept;
    [[nodiscard]] const KnownPluginList& knownPlugins() const noexcept;
    [[nodiscard]] PluginScanError loadCache();
    [[nodiscard]] PluginScanResult scanOnBackgroundWorker(const PluginScanDescriptor& descriptor,
                                                          core::BackgroundJobTicket ticket,
                                                          PluginScanWorkerModel& worker);
    [[nodiscard]] PluginChainRecovery recoverSavedPluginChain(const core::PluginChainDescriptor& chain) const;

private:
    persistence::PluginScanCacheRepository repository_;
    AudioPluginFormatManager formats_;
    KnownPluginList knownPlugins_;
};

[[nodiscard]] bool isSupportedVst3Path(const std::string& path) noexcept;
[[nodiscard]] std::string pluginIdFromPath(const std::string& path);
[[nodiscard]] std::string blacklistIdFromPath(const std::string& path);

}
