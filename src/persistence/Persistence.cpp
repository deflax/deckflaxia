#include "persistence/Persistence.h"

#include <utility>

namespace djapp::persistence {

namespace {

bool missingKey(const std::string& key) {
    return key.empty();
}

PersistenceUnitResult ensureWritable(const InMemoryPersistenceStore& store) {
    if (store.locked()) {
        return PersistenceUnitResult::failure(PersistenceError::DatabaseLocked);
    }
    return PersistenceUnitResult::success();
}

template <typename Map>
auto mapValues(const Map& map) {
    std::vector<typename Map::mapped_type> values;
    values.reserve(map.size());
    for (const auto& entry : map) {
        values.push_back(entry.second);
    }
    return values;
}

}

bool isRecoverable(PersistenceError error) noexcept {
    return error == PersistenceError::DatabaseLocked || error == PersistenceError::MigrationFailed || error == PersistenceError::WorkerUnavailable;
}

int InMemoryPersistenceStore::schemaVersion() const noexcept {
    return schemaVersion_;
}

void InMemoryPersistenceStore::setLockedForTest(bool locked) noexcept {
    locked_ = locked;
}

bool InMemoryPersistenceStore::locked() const noexcept {
    return locked_;
}

PersistenceUnitResult MigrationRunner::migrateToCurrent(InMemoryPersistenceStore& store) const {
    if (store.locked()) {
        return PersistenceUnitResult::failure(PersistenceError::DatabaseLocked);
    }
    if (store.schemaVersion_ > kCurrentSchemaVersion) {
        return PersistenceUnitResult::failure(PersistenceError::MigrationFailed);
    }
    store.schemaVersion_ = kCurrentSchemaVersion;
    return PersistenceUnitResult::success();
}

PersistenceUnitResult MigrationRunner::runFailingMigrationForTest(InMemoryPersistenceStore& store) const {
    if (store.locked()) {
        return PersistenceUnitResult::failure(PersistenceError::DatabaseLocked);
    }
    return PersistenceUnitResult::failure(PersistenceError::MigrationFailed);
}

core::BackgroundWorkerRole DatabaseWorkerBoundary::role() const noexcept {
    return core::BackgroundWorkerRole::DatabaseWorker;
}

bool DatabaseWorkerBoundary::accepts(core::BackgroundJobTicket ticket) const noexcept {
    return ticket.role == core::BackgroundWorkerRole::DatabaseWorker && ticket.kind == core::BackgroundJobKind::PersistLibraryChange;
}

PersistenceUnitResult DatabaseWorkerBoundary::runMigration(core::BackgroundJobTicket ticket, InMemoryPersistenceStore& store) const {
    if (!accepts(ticket)) {
        return PersistenceUnitResult::failure(PersistenceError::WorkerUnavailable);
    }
    return MigrationRunner{}.migrateToCurrent(store);
}

AppPreferencesRepository::AppPreferencesRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult AppPreferencesRepository::put(AppPreference preference) {
    if (missingKey(preference.key)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->preferences_[std::move(preference.key)] = std::move(preference.value);
    return PersistenceUnitResult::success();
}

PersistenceResult<std::string> AppPreferencesRepository::get(const std::string& key) const {
    const auto found = store_->preferences_.find(key);
    if (found == store_->preferences_.end()) {
        return PersistenceResult<std::string>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<std::string>::success(found->second);
}

RoutingConfigRepository::RoutingConfigRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult RoutingConfigRepository::save(RoutingConfigRecord record) {
    if (record.deckIndex >= 4) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->routing_[record.deckIndex] = record.assignment;
    return PersistenceUnitResult::success();
}

PersistenceResult<RoutingConfigRecord> RoutingConfigRepository::load(std::size_t deckIndex) const {
    const auto found = store_->routing_.find(deckIndex);
    if (found == store_->routing_.end()) {
        return PersistenceResult<RoutingConfigRecord>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<RoutingConfigRecord>::success(RoutingConfigRecord{deckIndex, found->second});
}

PluginScanCacheRepository::PluginScanCacheRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult PluginScanCacheRepository::upsert(PluginScanCacheRecord record) {
    if (missingKey(record.pluginId)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->plugins_[record.pluginId] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceUnitResult PluginScanCacheRepository::markBlacklisted(std::string pluginId, bool blacklisted) {
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    auto found = store_->plugins_.find(pluginId);
    if (found == store_->plugins_.end()) {
        return PersistenceUnitResult::failure(PersistenceError::NotFound);
    }
    found->second.blacklisted = blacklisted;
    return PersistenceUnitResult::success();
}

PersistenceResult<std::vector<PluginScanCacheRecord>> PluginScanCacheRepository::list() const {
    return PersistenceResult<std::vector<PluginScanCacheRecord>>::success(mapValues(store_->plugins_));
}

MidiMappingsRepository::MidiMappingsRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult MidiMappingsRepository::save(core::MidiLearnMapping mapping) {
    if (missingKey(mapping.target.id)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->midiMappings_[mapping.target.id] = std::move(mapping);
    return PersistenceUnitResult::success();
}

PersistenceResult<std::vector<core::MidiLearnMapping>> MidiMappingsRepository::list() const {
    return PersistenceResult<std::vector<core::MidiLearnMapping>>::success(mapValues(store_->midiMappings_));
}

LibraryTracksRepository::LibraryTracksRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult LibraryTracksRepository::upsert(core::LibraryTrack track) {
    if (missingKey(track.id)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->tracks_[track.id] = std::move(track);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::LibraryTrack> LibraryTracksRepository::findById(const std::string& id) const {
    const auto found = store_->tracks_.find(id);
    if (found == store_->tracks_.end()) {
        return PersistenceResult<core::LibraryTrack>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::LibraryTrack>::success(found->second);
}

PersistenceResult<std::vector<core::LibraryTrack>> LibraryTracksRepository::list() const {
    return PersistenceResult<std::vector<core::LibraryTrack>>::success(mapValues(store_->tracks_));
}

CratesRepository::CratesRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult CratesRepository::save(core::Crate crate) {
    if (missingKey(crate.id)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->crates_[crate.id] = std::move(crate);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::Crate> CratesRepository::findById(const std::string& id) const {
    const auto found = store_->crates_.find(id);
    if (found == store_->crates_.end()) {
        return PersistenceResult<core::Crate>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::Crate>::success(found->second);
}

PlaylistsRepository::PlaylistsRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult PlaylistsRepository::save(core::Playlist playlist) {
    if (missingKey(playlist.id)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->playlists_[playlist.id] = std::move(playlist);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::Playlist> PlaylistsRepository::findById(const std::string& id) const {
    const auto found = store_->playlists_.find(id);
    if (found == store_->playlists_.end()) {
        return PersistenceResult<core::Playlist>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::Playlist>::success(found->second);
}

TrackMetadataRepository::TrackMetadataRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult TrackMetadataRepository::save(TrackMetadataRecord record) {
    if (missingKey(record.trackId)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->metadata_[record.trackId] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceResult<TrackMetadataRecord> TrackMetadataRepository::load(const std::string& trackId) const {
    const auto found = store_->metadata_.find(trackId);
    if (found == store_->metadata_.end()) {
        return PersistenceResult<TrackMetadataRecord>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<TrackMetadataRecord>::success(found->second);
}

AnalysisJobsRepository::AnalysisJobsRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult AnalysisJobsRepository::upsert(core::AnalysisJob job) {
    if (missingKey(job.id) || missingKey(job.trackId)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
    store_->analysisJobs_[job.id] = std::move(job);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::AnalysisJob> AnalysisJobsRepository::findById(const std::string& id) const {
    const auto found = store_->analysisJobs_.find(id);
    if (found == store_->analysisJobs_.end()) {
        return PersistenceResult<core::AnalysisJob>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::AnalysisJob>::success(found->second);
}

PersistenceResult<std::vector<core::AnalysisJob>> AnalysisJobsRepository::list() const {
    return PersistenceResult<std::vector<core::AnalysisJob>>::success(mapValues(store_->analysisJobs_));
}

PersistenceService::PersistenceService() : store_(std::make_shared<InMemoryPersistenceStore>()) {}

SQLiteIntegrationDecision PersistenceService::sqliteDecision() const noexcept {
    return SQLiteIntegrationDecision{};
}

InMemoryPersistenceStore& PersistenceService::store() noexcept {
    return *store_;
}

const InMemoryPersistenceStore& PersistenceService::store() const noexcept {
    return *store_;
}

PersistenceUnitResult PersistenceService::migrateOnDatabaseWorker(core::BackgroundJobTicket ticket) {
    return worker_.runMigration(ticket, *store_);
}

AppPreferencesRepository PersistenceService::appPreferences() const { return AppPreferencesRepository{store_}; }
RoutingConfigRepository PersistenceService::routingConfig() const { return RoutingConfigRepository{store_}; }
PluginScanCacheRepository PersistenceService::pluginScanCache() const { return PluginScanCacheRepository{store_}; }
MidiMappingsRepository PersistenceService::midiMappings() const { return MidiMappingsRepository{store_}; }
LibraryTracksRepository PersistenceService::libraryTracks() const { return LibraryTracksRepository{store_}; }
CratesRepository PersistenceService::crates() const { return CratesRepository{store_}; }
PlaylistsRepository PersistenceService::playlists() const { return PlaylistsRepository{store_}; }
TrackMetadataRepository PersistenceService::trackMetadata() const { return TrackMetadataRepository{store_}; }
AnalysisJobsRepository PersistenceService::analysisJobs() const { return AnalysisJobsRepository{store_}; }

}
