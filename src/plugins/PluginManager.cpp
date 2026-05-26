#include "plugins/PluginManager.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace djapp::plugins {

namespace {

bool sameString(const std::string& left, const std::string& right) noexcept {
    return left == right;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) noexcept {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool repositoryOk(persistence::PersistenceUnitResult result) noexcept {
    return result.ok();
}

PluginDescriptor descriptorFromRecord(const persistence::PluginScanCacheRecord& record) {
    return PluginDescriptor{record.pluginId, record.displayName, record.path, PluginFormatKind::VST3};
}

}

AudioPluginFormatManager::AudioPluginFormatManager() noexcept = default;

std::size_t AudioPluginFormatManager::formatCount() const noexcept {
    return vst3Registered_ ? 1U : 0U;
}

bool AudioPluginFormatManager::supportsFormat(PluginFormatKind format) const noexcept {
    return vst3Registered_ && format == PluginFormatKind::VST3;
}

bool AudioPluginFormatManager::onlyVst3Enabled() const noexcept {
    return supportsFormat(PluginFormatKind::VST3) && formatCount() == 1U;
}

PluginScanError KnownPluginList::loadFromRepository(const persistence::PluginScanCacheRepository& repository) {
    const auto records = repository.list();
    if (!records.ok()) {
        return PluginScanError::RepositoryFailure;
    }

    available_.clear();
    blacklistedPaths_.clear();
    for (const auto& record : records.value) {
        if (record.blacklisted) {
            blacklistedPaths_.push_back(record.path);
        } else {
            available_.push_back(descriptorFromRecord(record));
        }
    }
    return PluginScanError::None;
}

PluginScanError KnownPluginList::addOrUpdate(const PluginDescriptor& descriptor, persistence::PluginScanCacheRepository& repository) {
    if (!repositoryOk(repository.upsert(persistence::PluginScanCacheRecord{descriptor.pluginId, descriptor.displayName, descriptor.path, false}))) {
        return PluginScanError::RepositoryFailure;
    }

    auto found = std::find_if(available_.begin(), available_.end(), [&](const PluginDescriptor& known) {
        return sameString(known.pluginId, descriptor.pluginId);
    });
    if (found == available_.end()) {
        available_.push_back(descriptor);
    } else {
        *found = descriptor;
    }
    return PluginScanError::None;
}

PluginScanError KnownPluginList::blacklistPath(const std::string& path, persistence::PluginScanCacheRepository& repository) {
    const auto record = persistence::PluginScanCacheRecord{blacklistIdFromPath(path), "failed plugin candidate", path, true};
    if (!repositoryOk(repository.upsert(record))) {
        return PluginScanError::RepositoryFailure;
    }
    if (std::find(blacklistedPaths_.begin(), blacklistedPaths_.end(), path) == blacklistedPaths_.end()) {
        blacklistedPaths_.push_back(path);
    }
    return PluginScanError::None;
}

bool KnownPluginList::contains(const std::string& pluginId) const noexcept {
    return std::find_if(available_.begin(), available_.end(), [&](const PluginDescriptor& known) {
               return sameString(known.pluginId, pluginId);
           }) != available_.end();
}

bool KnownPluginList::isPathBlacklisted(const std::string& path) const noexcept {
    return std::find(blacklistedPaths_.begin(), blacklistedPaths_.end(), path) != blacklistedPaths_.end();
}

const std::vector<PluginDescriptor>& KnownPluginList::availablePlugins() const noexcept {
    return available_;
}

bool PluginScanWorkerModel::trySchedule(core::BackgroundJobTicket ticket) noexcept {
    if (ticket.kind != core::BackgroundJobKind::ScanPlugins || ticket.role != core::BackgroundWorkerRole::PluginScanWorker) {
        return false;
    }
    scheduled_ = true;
    stopRequested_ = false;
    return true;
}

void PluginScanWorkerModel::requestStop() noexcept {
    stopRequested_ = true;
}

bool PluginScanWorkerModel::stopRequested() const noexcept {
    return stopRequested_;
}

bool PluginScanWorkerModel::scheduled() const noexcept {
    return scheduled_;
}

void PluginScanWorkerModel::requestStopAfterCandidateCount(std::size_t candidateCount) noexcept {
    stopAfterCandidateCount_ = candidateCount;
}

bool PluginScanWorkerModel::shouldStopAfterCandidate(std::size_t processedCandidateCount) const noexcept {
    return stopAfterCandidateCount_ > 0U && processedCandidateCount >= stopAfterCandidateCount_;
}

PluginDirectoryScanner::PluginDirectoryScanner(persistence::PluginScanCacheRepository repository) : repository_(std::move(repository)) {}

PluginScanResult PluginDirectoryScanner::scan(const PluginScanDescriptor& descriptor,
                                              core::BackgroundJobTicket ticket,
                                              PluginScanWorkerModel& worker,
                                              KnownPluginList& knownPlugins) {
    PluginScanResult result;
    if (!worker.trySchedule(ticket)) {
        result.error = PluginScanError::WorkerUnavailable;
        return result;
    }

    std::size_t processedCandidateCount{};
    for (const auto& candidate : descriptor.candidates) {
        if (worker.stopRequested()) {
            result.cancelled = true;
            break;
        }
        if (knownPlugins.isPathBlacklisted(candidate.path)) {
            result.blacklistedPaths.push_back(candidate.path);
            ++processedCandidateCount;
            if (worker.shouldStopAfterCandidate(processedCandidateCount)) {
                worker.requestStop();
            }
            continue;
        }
        if (!isSupportedVst3Path(candidate.path)) {
            const auto blacklistResult = knownPlugins.blacklistPath(candidate.path, repository_);
            if (blacklistResult != PluginScanError::None) {
                result.error = blacklistResult;
                return result;
            }
            result.blacklistedPaths.push_back(candidate.path);
            ++processedCandidateCount;
            if (worker.shouldStopAfterCandidate(processedCandidateCount)) {
                worker.requestStop();
            }
            continue;
        }

        const PluginDescriptor plugin{pluginIdFromPath(candidate.path), candidate.displayName, candidate.path, PluginFormatKind::VST3};
        const auto addResult = knownPlugins.addOrUpdate(plugin, repository_);
        if (addResult != PluginScanError::None) {
            result.error = addResult;
            return result;
        }
        result.discovered.push_back(plugin);
        ++processedCandidateCount;
        if (worker.shouldStopAfterCandidate(processedCandidateCount)) {
            worker.requestStop();
        }
    }
    return result;
}

audio::routing::RoutingGraphResult PluginGraphCommandModel::insertManualPlugin(audio::routing::AudioRoutingGraphController& graph,
                                                                               core::DeckId deckId,
                                                                               std::size_t slotIndex,
                                                                               PluginSlotRecoveryState state) const noexcept {
    return graph.enqueueInsertPluginSlot(deckId, slotIndex, state == PluginSlotRecoveryState::MissingPluginPlaceholder);
}

audio::routing::RoutingGraphResult PluginGraphCommandModel::removeManualPlugin(audio::routing::AudioRoutingGraphController& graph,
                                                                               core::DeckId deckId,
                                                                               std::size_t slotIndex) const noexcept {
    return graph.enqueueRemovePluginSlot(deckId, slotIndex);
}

Vst3PluginManager::Vst3PluginManager(persistence::PluginScanCacheRepository repository) : repository_(std::move(repository)) {}

const AudioPluginFormatManager& Vst3PluginManager::formatManager() const noexcept {
    return formats_;
}

const KnownPluginList& Vst3PluginManager::knownPlugins() const noexcept {
    return knownPlugins_;
}

PluginScanError Vst3PluginManager::loadCache() {
    return knownPlugins_.loadFromRepository(repository_);
}

PluginScanResult Vst3PluginManager::scanOnBackgroundWorker(const PluginScanDescriptor& descriptor,
                                                           core::BackgroundJobTicket ticket,
                                                           PluginScanWorkerModel& worker) {
    PluginDirectoryScanner scanner(repository_);
    return scanner.scan(descriptor, ticket, worker, knownPlugins_);
}

PluginChainRecovery Vst3PluginManager::recoverSavedPluginChain(const core::PluginChainDescriptor& chain) const {
    PluginChainRecovery recovery;
    recovery.chainId = chain.identifier;
    recovery.slots.reserve(chain.plugins.size());
    for (const auto& savedPlugin : chain.plugins) {
        recovery.slots.push_back(PluginRecoverySlot{savedPlugin,
                                                    knownPlugins_.contains(savedPlugin.identifier)
                                                        ? PluginSlotRecoveryState::Available
                                                        : PluginSlotRecoveryState::MissingPluginPlaceholder});
    }
    return recovery;
}

bool isSupportedVst3Path(const std::string& path) noexcept {
    return endsWith(lowerCopy(path), ".vst3");
}

std::string pluginIdFromPath(const std::string& path) {
    return std::string{"vst3:"} + path;
}

std::string blacklistIdFromPath(const std::string& path) {
    return std::string{"failed:"} + path;
}

}
