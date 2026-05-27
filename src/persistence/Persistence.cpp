#include "persistence/Persistence.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>

#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
#include <sqlite3.h>
#endif

namespace deckflaxia::persistence {

namespace {

template <typename Enum>
int enumToInt(Enum value) noexcept {
    return static_cast<int>(value);
}

template <typename Enum>
Enum enumFromInt(int value) noexcept {
    return static_cast<Enum>(value);
}

std::string joinIds(const std::vector<std::string>& ids) {
    std::string joined;
    for (const auto& id : ids) {
        if (!joined.empty()) {
            joined.push_back('\n');
        }
        joined += id;
    }
    return joined;
}

std::vector<std::string> splitIds(const std::string& value) {
    std::vector<std::string> ids;
    std::stringstream stream(value);
    std::string id;
    while (std::getline(stream, id)) {
        if (!id.empty()) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::string serializeCueMarkers(const std::vector<CueMarkerRecord>& markers) {
    std::string serialized;
    for (const auto& marker : markers) {
        if (!serialized.empty()) {
            serialized.push_back('\n');
        }
        serialized += marker.id + "|" + std::to_string(marker.seconds) + "|" + marker.label;
    }
    return serialized;
}

std::vector<CueMarkerRecord> deserializeCueMarkers(const std::string& value) {
    std::vector<CueMarkerRecord> markers;
    std::stringstream stream(value);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find('|');
        const auto second = first == std::string::npos ? std::string::npos : line.find('|', first + 1U);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        markers.push_back(CueMarkerRecord{line.substr(0, first), std::stod(line.substr(first + 1U, second - first - 1U)), line.substr(second + 1U)});
    }
    return markers;
}

std::string serializePluginParameters(const std::vector<core::PluginDescriptor::ParameterState>& parameters) {
    std::string serialized;
    for (const auto& parameter : parameters) {
        if (!serialized.empty()) {
            serialized.push_back('\n');
        }
        serialized += parameter.identifier + "|" + parameter.displayName + "|" + std::to_string(parameter.normalizedValue);
    }
    return serialized;
}

std::vector<core::PluginDescriptor::ParameterState> deserializePluginParameters(const std::string& value) {
    std::vector<core::PluginDescriptor::ParameterState> parameters;
    std::stringstream stream(value);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find('|');
        const auto second = first == std::string::npos ? std::string::npos : line.find('|', first + 1U);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        parameters.push_back(core::PluginDescriptor::ParameterState{line.substr(0, first), line.substr(first + 1U, second - first - 1U), std::stod(line.substr(second + 1U))});
    }
    return parameters;
}

struct Statement final {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    sqlite3_stmt* handle{};
    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Statement& operator=(Statement&& other) noexcept {
        if (this != &other) {
            sqlite3_finalize(handle);
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Statement() { sqlite3_finalize(handle); }
#endif
};

}

struct SQLiteConnection final {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    sqlite3* db{};
    bool open{};
    PersistenceError openError{PersistenceError::None};
    ~SQLiteConnection() { sqlite3_close(db); }
#else
    bool open{};
    PersistenceError openError{PersistenceError::OpenFailed};
#endif
};

namespace {

#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE

PersistenceError sqliteError(int code) noexcept {
    if (code == SQLITE_BUSY || code == SQLITE_LOCKED) {
        return PersistenceError::DatabaseLocked;
    }
    return PersistenceError::MigrationFailed;
}

PersistenceUnitResult execSql(const std::shared_ptr<SQLiteConnection>& connection, const char* sql) {
    char* errorMessage{};
    const int result = sqlite3_exec(connection->db, sql, nullptr, nullptr, &errorMessage);
    sqlite3_free(errorMessage);
    if (result != SQLITE_OK) {
        return PersistenceUnitResult::failure(sqliteError(result));
    }
    return PersistenceUnitResult::success();
}

PersistenceResult<Statement> prepareSql(const std::shared_ptr<SQLiteConnection>& connection, const char* sql) {
    Statement statement;
    const int result = sqlite3_prepare_v2(connection->db, sql, -1, &statement.handle, nullptr);
    if (result != SQLITE_OK) {
        return PersistenceResult<Statement>::failure(sqliteError(result));
    }
    return PersistenceResult<Statement>::success(std::move(statement));
}

PersistenceUnitResult stepDone(sqlite3_stmt* statement) {
    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        return PersistenceUnitResult::failure(sqliteError(result));
    }
    return PersistenceUnitResult::success();
}

PersistenceUnitResult bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    const int result = sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
    return result == SQLITE_OK ? PersistenceUnitResult::success() : PersistenceUnitResult::failure(sqliteError(result));
}

std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    return text == nullptr ? std::string{} : reinterpret_cast<const char*>(text);
}

PersistenceUnitResult runMigrationSql(const std::shared_ptr<SQLiteConnection>& connection) {
    const auto begin = execSql(connection, "BEGIN IMMEDIATE");
    if (!begin.ok()) {
        return begin;
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS preferences(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS routing_config(deck_index INTEGER PRIMARY KEY, main_output INTEGER NOT NULL, cue_enabled INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS plugin_scan_cache(plugin_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, path TEXT NOT NULL, blacklisted INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS midi_mappings(target_id TEXT PRIMARY KEY, target_name TEXT NOT NULL, channel INTEGER NOT NULL, controller INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS library_tracks(track_id TEXT PRIMARY KEY, title TEXT NOT NULL, artist TEXT NOT NULL, bpm REAL NOT NULL, first_beat_seconds REAL NOT NULL, musical_key INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS crates(crate_id TEXT PRIMARY KEY, name TEXT NOT NULL, track_ids TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS playlists(playlist_id TEXT PRIMARY KEY, name TEXT NOT NULL, track_ids TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS track_metadata(track_id TEXT PRIMARY KEY, bpm REAL NOT NULL, first_beat_seconds REAL NOT NULL, musical_key INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS cue_markers(track_id TEXT PRIMARY KEY, markers TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS analysis_jobs(job_id TEXT PRIMARY KEY, track_id TEXT NOT NULL, status INTEGER NOT NULL, progress REAL NOT NULL);"
        "CREATE TABLE IF NOT EXISTS deck_state(deck_index INTEGER PRIMARY KEY, deck_type INTEGER NOT NULL, main_output INTEGER NOT NULL, cue_enabled INTEGER NOT NULL, playing INTEGER NOT NULL, position_beats REAL NOT NULL, loaded_track_id TEXT NOT NULL, source_bpm REAL NOT NULL DEFAULT 120.0, target_bpm REAL NOT NULL DEFAULT 120.0, tempo_sync_enabled INTEGER NOT NULL DEFAULT 0, pitch_lock_enabled INTEGER NOT NULL DEFAULT 1, pitch_shift_cents REAL NOT NULL DEFAULT 0.0, stretch_bypass INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS plugin_chains(chain_id TEXT PRIMARY KEY);"
        "CREATE TABLE IF NOT EXISTS plugin_chain_slots(chain_id TEXT NOT NULL, slot_index INTEGER NOT NULL, plugin_id TEXT NOT NULL, display_name TEXT NOT NULL, bypassed INTEGER NOT NULL, parameter_state TEXT NOT NULL DEFAULT '', latency_frames INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(chain_id, slot_index));"
        "CREATE TABLE IF NOT EXISTS audio_device_preferences(device_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, sample_rate_hz INTEGER NOT NULL, buffer_frames INTEGER NOT NULL, degraded INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS sandbox_health(component TEXT PRIMARY KEY, healthy INTEGER NOT NULL, detail TEXT NOT NULL);";
    const auto created = execSql(connection, schema);
    if (!created.ok()) {
        (void)execSql(connection, "ROLLBACK");
        return created;
    }
    const auto cleared = execSql(connection, "DELETE FROM schema_version");
    if (!cleared.ok()) {
        (void)execSql(connection, "ROLLBACK");
        return cleared;
    }
    auto version = prepareSql(connection, "INSERT INTO schema_version(version) VALUES(?)");
    if (!version.ok()) {
        (void)execSql(connection, "ROLLBACK");
        return PersistenceUnitResult::failure(version.error);
    }
    sqlite3_bind_int(version.value.handle, 1, kCurrentSchemaVersion);
    const auto stepped = stepDone(version.value.handle);
    if (!stepped.ok()) {
        (void)execSql(connection, "ROLLBACK");
        return stepped;
    }
    return execSql(connection, "COMMIT");
}

PersistenceResult<int> loadSchemaVersionSql(const std::shared_ptr<SQLiteConnection>& connection) {
    auto statement = prepareSql(connection, "SELECT version FROM schema_version LIMIT 1");
    if (!statement.ok()) {
        return PersistenceResult<int>::failure(PersistenceError::InvalidSchema);
    }
    const int result = sqlite3_step(statement.value.handle);
    if (result == SQLITE_ROW) {
        return PersistenceResult<int>::success(sqlite3_column_int(statement.value.handle, 0));
    }
    return PersistenceResult<int>::failure(PersistenceError::InvalidSchema);
}

#endif

bool missingKey(const std::string& key) {
    return key.empty();
}

bool usesSQLite(const InMemoryPersistenceStore& store) noexcept {
    return store.sqliteOpen();
}

PersistenceUnitResult ensureWritable(const InMemoryPersistenceStore& store) {
    if (store.locked()) {
        return PersistenceUnitResult::failure(PersistenceError::DatabaseLocked);
    }
    if (store.sqliteOpenError() != PersistenceError::None) {
        return PersistenceUnitResult::failure(store.sqliteOpenError());
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
    return error == PersistenceError::DatabaseLocked || error == PersistenceError::MigrationFailed || error == PersistenceError::OpenFailed || error == PersistenceError::WorkerUnavailable;
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

bool InMemoryPersistenceStore::sqliteOpen() const noexcept {
    return sqlite_ != nullptr && sqlite_->open;
}

PersistenceError InMemoryPersistenceStore::sqliteOpenError() const noexcept {
    if (sqlite_ == nullptr || sqlite_->open) {
        return PersistenceError::None;
    }
    return sqlite_->openError;
}

PersistenceUnitResult MigrationRunner::migrateToCurrent(InMemoryPersistenceStore& store) const {
    if (store.locked()) {
        return PersistenceUnitResult::failure(PersistenceError::DatabaseLocked);
    }
    if (store.sqlite_ != nullptr) {
        if (!store.sqlite_->open) {
            return PersistenceUnitResult::failure(store.sqlite_->openError);
        }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
        const auto migrated = runMigrationSql(store.sqlite_);
        if (!migrated.ok()) {
            return migrated;
        }
        const auto version = loadSchemaVersionSql(store.sqlite_);
        if (!version.ok()) {
            return PersistenceUnitResult::failure(version.error);
        }
        if (version.value != kCurrentSchemaVersion) {
            return PersistenceUnitResult::failure(PersistenceError::InvalidSchema);
        }
        store.schemaVersion_ = version.value;
        return PersistenceUnitResult::success();
#else
        return PersistenceUnitResult::failure(PersistenceError::OpenFailed);
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO preferences(key, value) VALUES(?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        const auto key = bindText(statement.value.handle, 1, preference.key);
        const auto value = bindText(statement.value.handle, 2, preference.value);
        if (!key.ok() || !value.ok()) {
            return PersistenceUnitResult::failure(key.ok() ? value.error : key.error);
        }
        return stepDone(statement.value.handle);
    }
#endif
    store_->preferences_[std::move(preference.key)] = std::move(preference.value);
    return PersistenceUnitResult::success();
}

PersistenceResult<std::string> AppPreferencesRepository::get(const std::string& key) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT value FROM preferences WHERE key=?");
        if (!statement.ok()) {
            return PersistenceResult<std::string>::failure(statement.error);
        }
        const auto bound = bindText(statement.value.handle, 1, key);
        if (!bound.ok()) {
            return PersistenceResult<std::string>::failure(bound.error);
        }
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<std::string>::success(columnText(statement.value.handle, 0));
        }
        return result == SQLITE_DONE ? PersistenceResult<std::string>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<std::string>::failure(sqliteError(result));
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO routing_config(deck_index, main_output, cue_enabled) VALUES(?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        sqlite3_bind_int(statement.value.handle, 1, static_cast<int>(record.deckIndex));
        sqlite3_bind_int(statement.value.handle, 2, enumToInt(record.assignment.mainOutput));
        sqlite3_bind_int(statement.value.handle, 3, record.assignment.cueEnabled ? 1 : 0);
        return stepDone(statement.value.handle);
    }
#endif
    store_->routing_[record.deckIndex] = record.assignment;
    return PersistenceUnitResult::success();
}

PersistenceResult<RoutingConfigRecord> RoutingConfigRepository::load(std::size_t deckIndex) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT main_output, cue_enabled FROM routing_config WHERE deck_index=?");
        if (!statement.ok()) {
            return PersistenceResult<RoutingConfigRecord>::failure(statement.error);
        }
        sqlite3_bind_int(statement.value.handle, 1, static_cast<int>(deckIndex));
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            RoutingConfigRecord record;
            record.deckIndex = deckIndex;
            record.assignment.mainOutput = enumFromInt<core::OutputBus>(sqlite3_column_int(statement.value.handle, 0));
            record.assignment.cueEnabled = sqlite3_column_int(statement.value.handle, 1) != 0;
            return PersistenceResult<RoutingConfigRecord>::success(record);
        }
        return result == SQLITE_DONE ? PersistenceResult<RoutingConfigRecord>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<RoutingConfigRecord>::failure(sqliteError(result));
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO plugin_scan_cache(plugin_id, display_name, path, blacklisted) VALUES(?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, record.pluginId);
        (void)bindText(statement.value.handle, 2, record.displayName);
        (void)bindText(statement.value.handle, 3, record.path);
        sqlite3_bind_int(statement.value.handle, 4, record.blacklisted ? 1 : 0);
        return stepDone(statement.value.handle);
    }
#endif
    store_->plugins_[record.pluginId] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceUnitResult PluginScanCacheRepository::markBlacklisted(std::string pluginId, bool blacklisted) {
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "UPDATE plugin_scan_cache SET blacklisted=? WHERE plugin_id=?");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        sqlite3_bind_int(statement.value.handle, 1, blacklisted ? 1 : 0);
        (void)bindText(statement.value.handle, 2, pluginId);
        const auto stepped = stepDone(statement.value.handle);
        if (!stepped.ok()) {
            return stepped;
        }
        return sqlite3_changes(store_->sqlite_->db) == 0 ? PersistenceUnitResult::failure(PersistenceError::NotFound) : PersistenceUnitResult::success();
    }
#endif
    auto found = store_->plugins_.find(pluginId);
    if (found == store_->plugins_.end()) {
        return PersistenceUnitResult::failure(PersistenceError::NotFound);
    }
    found->second.blacklisted = blacklisted;
    return PersistenceUnitResult::success();
}

PersistenceResult<std::vector<PluginScanCacheRecord>> PluginScanCacheRepository::list() const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT plugin_id, display_name, path, blacklisted FROM plugin_scan_cache ORDER BY plugin_id");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<PluginScanCacheRecord>>::failure(statement.error);
        }
        std::vector<PluginScanCacheRecord> records;
        for (;;) {
            const int result = sqlite3_step(statement.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<std::vector<PluginScanCacheRecord>>::success(records);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<std::vector<PluginScanCacheRecord>>::failure(sqliteError(result));
            }
            records.push_back(PluginScanCacheRecord{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), columnText(statement.value.handle, 2), sqlite3_column_int(statement.value.handle, 3) != 0});
        }
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO midi_mappings(target_id, target_name, channel, controller) VALUES(?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, mapping.target.id);
        (void)bindText(statement.value.handle, 2, mapping.target.displayName);
        sqlite3_bind_int(statement.value.handle, 3, mapping.message.channel);
        sqlite3_bind_int(statement.value.handle, 4, mapping.message.controller);
        return stepDone(statement.value.handle);
    }
#endif
    store_->midiMappings_[mapping.target.id] = std::move(mapping);
    return PersistenceUnitResult::success();
}

PersistenceResult<std::vector<core::MidiLearnMapping>> MidiMappingsRepository::list() const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT target_id, target_name, channel, controller FROM midi_mappings ORDER BY target_id");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<core::MidiLearnMapping>>::failure(statement.error);
        }
        std::vector<core::MidiLearnMapping> records;
        for (;;) {
            const int result = sqlite3_step(statement.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<std::vector<core::MidiLearnMapping>>::success(records);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<std::vector<core::MidiLearnMapping>>::failure(sqliteError(result));
            }
            records.push_back(core::MidiLearnMapping{core::MidiLearnTarget{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1)}, core::MidiMessageDescriptor{sqlite3_column_int(statement.value.handle, 2), sqlite3_column_int(statement.value.handle, 3)}});
        }
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO library_tracks(track_id, title, artist, bpm, first_beat_seconds, musical_key) VALUES(?, ?, ?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, track.id);
        (void)bindText(statement.value.handle, 2, track.title);
        (void)bindText(statement.value.handle, 3, track.artist);
        sqlite3_bind_double(statement.value.handle, 4, track.beatgrid.bpm);
        sqlite3_bind_double(statement.value.handle, 5, track.beatgrid.firstBeatSeconds);
        sqlite3_bind_int(statement.value.handle, 6, enumToInt(track.key));
        return stepDone(statement.value.handle);
    }
#endif
    store_->tracks_[track.id] = std::move(track);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::LibraryTrack> LibraryTracksRepository::findById(const std::string& id) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT track_id, title, artist, bpm, first_beat_seconds, musical_key FROM library_tracks WHERE track_id=?");
        if (!statement.ok()) {
            return PersistenceResult<core::LibraryTrack>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, id);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<core::LibraryTrack>::success(core::LibraryTrack{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), columnText(statement.value.handle, 2), core::BeatgridMetadata{sqlite3_column_double(statement.value.handle, 3), sqlite3_column_double(statement.value.handle, 4)}, enumFromInt<core::MusicalKey>(sqlite3_column_int(statement.value.handle, 5))});
        }
        return result == SQLITE_DONE ? PersistenceResult<core::LibraryTrack>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<core::LibraryTrack>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->tracks_.find(id);
    if (found == store_->tracks_.end()) {
        return PersistenceResult<core::LibraryTrack>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::LibraryTrack>::success(found->second);
}

PersistenceResult<std::vector<core::LibraryTrack>> LibraryTracksRepository::list() const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT track_id, title, artist, bpm, first_beat_seconds, musical_key FROM library_tracks ORDER BY track_id");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<core::LibraryTrack>>::failure(statement.error);
        }
        std::vector<core::LibraryTrack> records;
        for (;;) {
            const int result = sqlite3_step(statement.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<std::vector<core::LibraryTrack>>::success(records);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<std::vector<core::LibraryTrack>>::failure(sqliteError(result));
            }
            records.push_back(core::LibraryTrack{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), columnText(statement.value.handle, 2), core::BeatgridMetadata{sqlite3_column_double(statement.value.handle, 3), sqlite3_column_double(statement.value.handle, 4)}, enumFromInt<core::MusicalKey>(sqlite3_column_int(statement.value.handle, 5))});
        }
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO crates(crate_id, name, track_ids) VALUES(?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, crate.id);
        (void)bindText(statement.value.handle, 2, crate.name);
        const auto tracks = joinIds(crate.trackIds);
        (void)bindText(statement.value.handle, 3, tracks);
        return stepDone(statement.value.handle);
    }
#endif
    store_->crates_[crate.id] = std::move(crate);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::Crate> CratesRepository::findById(const std::string& id) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT crate_id, name, track_ids FROM crates WHERE crate_id=?");
        if (!statement.ok()) {
            return PersistenceResult<core::Crate>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, id);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<core::Crate>::success(core::Crate{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), splitIds(columnText(statement.value.handle, 2))});
        }
        return result == SQLITE_DONE ? PersistenceResult<core::Crate>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<core::Crate>::failure(sqliteError(result));
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO playlists(playlist_id, name, track_ids) VALUES(?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, playlist.id);
        (void)bindText(statement.value.handle, 2, playlist.name);
        const auto tracks = joinIds(playlist.trackIds);
        (void)bindText(statement.value.handle, 3, tracks);
        return stepDone(statement.value.handle);
    }
#endif
    store_->playlists_[playlist.id] = std::move(playlist);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::Playlist> PlaylistsRepository::findById(const std::string& id) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT playlist_id, name, track_ids FROM playlists WHERE playlist_id=?");
        if (!statement.ok()) {
            return PersistenceResult<core::Playlist>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, id);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<core::Playlist>::success(core::Playlist{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), splitIds(columnText(statement.value.handle, 2))});
        }
        return result == SQLITE_DONE ? PersistenceResult<core::Playlist>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<core::Playlist>::failure(sqliteError(result));
    }
#endif
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO track_metadata(track_id, bpm, first_beat_seconds, musical_key) VALUES(?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, record.trackId);
        sqlite3_bind_double(statement.value.handle, 2, record.beatgrid.bpm);
        sqlite3_bind_double(statement.value.handle, 3, record.beatgrid.firstBeatSeconds);
        sqlite3_bind_int(statement.value.handle, 4, enumToInt(record.key));
        return stepDone(statement.value.handle);
    }
#endif
    store_->metadata_[record.trackId] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceResult<TrackMetadataRecord> TrackMetadataRepository::load(const std::string& trackId) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT track_id, bpm, first_beat_seconds, musical_key FROM track_metadata WHERE track_id=?");
        if (!statement.ok()) {
            return PersistenceResult<TrackMetadataRecord>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, trackId);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<TrackMetadataRecord>::success(TrackMetadataRecord{columnText(statement.value.handle, 0), core::BeatgridMetadata{sqlite3_column_double(statement.value.handle, 1), sqlite3_column_double(statement.value.handle, 2)}, enumFromInt<core::MusicalKey>(sqlite3_column_int(statement.value.handle, 3))});
        }
        return result == SQLITE_DONE ? PersistenceResult<TrackMetadataRecord>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<TrackMetadataRecord>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->metadata_.find(trackId);
    if (found == store_->metadata_.end()) {
        return PersistenceResult<TrackMetadataRecord>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<TrackMetadataRecord>::success(found->second);
}

PersistenceUnitResult TrackMetadataRepository::saveCueMarkers(const std::string& trackId, std::vector<CueMarkerRecord> markers) {
    if (missingKey(trackId)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO cue_markers(track_id, markers) VALUES(?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        const auto serialized = serializeCueMarkers(markers);
        (void)bindText(statement.value.handle, 1, trackId);
        (void)bindText(statement.value.handle, 2, serialized);
        return stepDone(statement.value.handle);
    }
#endif
    store_->cueMarkers_[trackId] = std::move(markers);
    return PersistenceUnitResult::success();
}

PersistenceResult<std::vector<CueMarkerRecord>> TrackMetadataRepository::loadCueMarkers(const std::string& trackId) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT markers FROM cue_markers WHERE track_id=?");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<CueMarkerRecord>>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, trackId);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<std::vector<CueMarkerRecord>>::success(deserializeCueMarkers(columnText(statement.value.handle, 0)));
        }
        return result == SQLITE_DONE ? PersistenceResult<std::vector<CueMarkerRecord>>::success({})
                                     : PersistenceResult<std::vector<CueMarkerRecord>>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->cueMarkers_.find(trackId);
    if (found == store_->cueMarkers_.end()) {
        return PersistenceResult<std::vector<CueMarkerRecord>>::success({});
    }
    return PersistenceResult<std::vector<CueMarkerRecord>>::success(found->second);
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
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO analysis_jobs(job_id, track_id, status, progress) VALUES(?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, job.id);
        (void)bindText(statement.value.handle, 2, job.trackId);
        sqlite3_bind_int(statement.value.handle, 3, enumToInt(job.status));
        sqlite3_bind_double(statement.value.handle, 4, job.progress);
        return stepDone(statement.value.handle);
    }
#endif
    store_->analysisJobs_[job.id] = std::move(job);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::AnalysisJob> AnalysisJobsRepository::findById(const std::string& id) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT job_id, track_id, status, progress FROM analysis_jobs WHERE job_id=?");
        if (!statement.ok()) {
            return PersistenceResult<core::AnalysisJob>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, id);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<core::AnalysisJob>::success(core::AnalysisJob{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), enumFromInt<core::AnalysisJobStatus>(sqlite3_column_int(statement.value.handle, 2)), sqlite3_column_double(statement.value.handle, 3)});
        }
        return result == SQLITE_DONE ? PersistenceResult<core::AnalysisJob>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<core::AnalysisJob>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->analysisJobs_.find(id);
    if (found == store_->analysisJobs_.end()) {
        return PersistenceResult<core::AnalysisJob>::failure(PersistenceError::NotFound);
    }
    return PersistenceResult<core::AnalysisJob>::success(found->second);
}

PersistenceResult<std::vector<core::AnalysisJob>> AnalysisJobsRepository::list() const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT job_id, track_id, status, progress FROM analysis_jobs ORDER BY job_id");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<core::AnalysisJob>>::failure(statement.error);
        }
        std::vector<core::AnalysisJob> records;
        for (;;) {
            const int result = sqlite3_step(statement.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<std::vector<core::AnalysisJob>>::success(records);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<std::vector<core::AnalysisJob>>::failure(sqliteError(result));
            }
            records.push_back(core::AnalysisJob{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), enumFromInt<core::AnalysisJobStatus>(sqlite3_column_int(statement.value.handle, 2)), sqlite3_column_double(statement.value.handle, 3)});
        }
    }
#endif
    return PersistenceResult<std::vector<core::AnalysisJob>>::success(mapValues(store_->analysisJobs_));
}

DeckStateRepository::DeckStateRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult DeckStateRepository::save(DeckStateRecord record) {
    if (record.deckIndex >= 4) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO deck_state(deck_index, deck_type, main_output, cue_enabled, playing, position_beats, loaded_track_id, source_bpm, target_bpm, tempo_sync_enabled, pitch_lock_enabled, pitch_shift_cents, stretch_bypass) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        sqlite3_bind_int(statement.value.handle, 1, static_cast<int>(record.deckIndex));
        sqlite3_bind_int(statement.value.handle, 2, enumToInt(record.type));
        sqlite3_bind_int(statement.value.handle, 3, enumToInt(record.routing.mainOutput));
        sqlite3_bind_int(statement.value.handle, 4, record.routing.cueEnabled ? 1 : 0);
        sqlite3_bind_int(statement.value.handle, 5, record.transport.playing ? 1 : 0);
        sqlite3_bind_double(statement.value.handle, 6, record.transport.positionBeats);
        (void)bindText(statement.value.handle, 7, record.loadedTrackId);
        sqlite3_bind_double(statement.value.handle, 8, record.tempoPitch.sourceBpm);
        sqlite3_bind_double(statement.value.handle, 9, record.tempoPitch.targetBpm);
        sqlite3_bind_int(statement.value.handle, 10, record.tempoPitch.tempoSyncEnabled ? 1 : 0);
        sqlite3_bind_int(statement.value.handle, 11, record.tempoPitch.pitchLockEnabled ? 1 : 0);
        sqlite3_bind_double(statement.value.handle, 12, record.tempoPitch.pitchShiftCents);
        sqlite3_bind_int(statement.value.handle, 13, record.tempoPitch.bypass ? 1 : 0);
        return stepDone(statement.value.handle);
    }
#endif
    store_->deckStates_[record.deckIndex] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceResult<DeckStateRecord> DeckStateRepository::load(std::size_t deckIndex) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT deck_index, deck_type, main_output, cue_enabled, playing, position_beats, loaded_track_id, source_bpm, target_bpm, tempo_sync_enabled, pitch_lock_enabled, pitch_shift_cents, stretch_bypass FROM deck_state WHERE deck_index=?");
        if (!statement.ok()) {
            return PersistenceResult<DeckStateRecord>::failure(statement.error);
        }
        sqlite3_bind_int(statement.value.handle, 1, static_cast<int>(deckIndex));
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            DeckStateRecord record;
            record.deckIndex = static_cast<std::size_t>(sqlite3_column_int(statement.value.handle, 0));
            record.type = enumFromInt<core::DeckType>(sqlite3_column_int(statement.value.handle, 1));
            record.routing.mainOutput = enumFromInt<core::OutputBus>(sqlite3_column_int(statement.value.handle, 2));
            record.routing.cueEnabled = sqlite3_column_int(statement.value.handle, 3) != 0;
            record.transport.playing = sqlite3_column_int(statement.value.handle, 4) != 0;
            record.transport.positionBeats = sqlite3_column_double(statement.value.handle, 5);
            record.loadedTrackId = columnText(statement.value.handle, 6);
            record.tempoPitch = core::TempoPitchSettings{sqlite3_column_double(statement.value.handle, 7), sqlite3_column_double(statement.value.handle, 8), sqlite3_column_int(statement.value.handle, 9) != 0, sqlite3_column_int(statement.value.handle, 10) != 0, sqlite3_column_double(statement.value.handle, 11), sqlite3_column_int(statement.value.handle, 12) != 0};
            return PersistenceResult<DeckStateRecord>::success(record);
        }
        return result == SQLITE_DONE ? PersistenceResult<DeckStateRecord>::failure(PersistenceError::NotFound)
                                     : PersistenceResult<DeckStateRecord>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->deckStates_.find(deckIndex);
    return found == store_->deckStates_.end() ? PersistenceResult<DeckStateRecord>::failure(PersistenceError::NotFound) : PersistenceResult<DeckStateRecord>::success(found->second);
}

PersistenceResult<std::vector<DeckStateRecord>> DeckStateRepository::list() const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT deck_index, deck_type, main_output, cue_enabled, playing, position_beats, loaded_track_id, source_bpm, target_bpm, tempo_sync_enabled, pitch_lock_enabled, pitch_shift_cents, stretch_bypass FROM deck_state ORDER BY deck_index");
        if (!statement.ok()) {
            return PersistenceResult<std::vector<DeckStateRecord>>::failure(statement.error);
        }
        std::vector<DeckStateRecord> records;
        for (;;) {
            const int result = sqlite3_step(statement.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<std::vector<DeckStateRecord>>::success(records);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<std::vector<DeckStateRecord>>::failure(sqliteError(result));
            }
            DeckStateRecord record;
            record.deckIndex = static_cast<std::size_t>(sqlite3_column_int(statement.value.handle, 0));
            record.type = enumFromInt<core::DeckType>(sqlite3_column_int(statement.value.handle, 1));
            record.routing.mainOutput = enumFromInt<core::OutputBus>(sqlite3_column_int(statement.value.handle, 2));
            record.routing.cueEnabled = sqlite3_column_int(statement.value.handle, 3) != 0;
            record.transport.playing = sqlite3_column_int(statement.value.handle, 4) != 0;
            record.transport.positionBeats = sqlite3_column_double(statement.value.handle, 5);
            record.loadedTrackId = columnText(statement.value.handle, 6);
            record.tempoPitch = core::TempoPitchSettings{sqlite3_column_double(statement.value.handle, 7), sqlite3_column_double(statement.value.handle, 8), sqlite3_column_int(statement.value.handle, 9) != 0, sqlite3_column_int(statement.value.handle, 10) != 0, sqlite3_column_double(statement.value.handle, 11), sqlite3_column_int(statement.value.handle, 12) != 0};
            records.push_back(record);
        }
    }
#endif
    return PersistenceResult<std::vector<DeckStateRecord>>::success(mapValues(store_->deckStates_));
}

PluginChainsRepository::PluginChainsRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult PluginChainsRepository::save(core::PluginChainDescriptor chain) {
    if (missingKey(chain.identifier)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        const auto begin = execSql(store_->sqlite_, "BEGIN IMMEDIATE");
        if (!begin.ok()) {
            return begin;
        }
        auto chainStatement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO plugin_chains(chain_id) VALUES(?)");
        if (!chainStatement.ok()) {
            (void)execSql(store_->sqlite_, "ROLLBACK");
            return PersistenceUnitResult::failure(chainStatement.error);
        }
        (void)bindText(chainStatement.value.handle, 1, chain.identifier);
        auto stepped = stepDone(chainStatement.value.handle);
        if (!stepped.ok()) {
            (void)execSql(store_->sqlite_, "ROLLBACK");
            return stepped;
        }
        auto clear = prepareSql(store_->sqlite_, "DELETE FROM plugin_chain_slots WHERE chain_id=?");
        (void)bindText(clear.value.handle, 1, chain.identifier);
        stepped = stepDone(clear.value.handle);
        if (!stepped.ok()) {
            (void)execSql(store_->sqlite_, "ROLLBACK");
            return stepped;
        }
        for (std::size_t slot = 0; slot < chain.plugins.size(); ++slot) {
            auto slotStatement = prepareSql(store_->sqlite_, "INSERT INTO plugin_chain_slots(chain_id, slot_index, plugin_id, display_name, bypassed, parameter_state, latency_frames) VALUES(?, ?, ?, ?, ?, ?, ?)");
            if (!slotStatement.ok()) {
                (void)execSql(store_->sqlite_, "ROLLBACK");
                return PersistenceUnitResult::failure(slotStatement.error);
            }
            (void)bindText(slotStatement.value.handle, 1, chain.identifier);
            sqlite3_bind_int(slotStatement.value.handle, 2, static_cast<int>(slot));
            (void)bindText(slotStatement.value.handle, 3, chain.plugins[slot].identifier);
            (void)bindText(slotStatement.value.handle, 4, chain.plugins[slot].displayName);
            sqlite3_bind_int(slotStatement.value.handle, 5, chain.plugins[slot].bypassed ? 1 : 0);
            (void)bindText(slotStatement.value.handle, 6, serializePluginParameters(chain.plugins[slot].parameters));
            sqlite3_bind_int(slotStatement.value.handle, 7, static_cast<int>(chain.plugins[slot].latencyFrames));
            stepped = stepDone(slotStatement.value.handle);
            if (!stepped.ok()) {
                (void)execSql(store_->sqlite_, "ROLLBACK");
                return stepped;
            }
        }
        return execSql(store_->sqlite_, "COMMIT");
    }
#endif
    store_->pluginChains_[chain.identifier] = std::move(chain);
    return PersistenceUnitResult::success();
}

PersistenceResult<core::PluginChainDescriptor> PluginChainsRepository::load(const std::string& chainId) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto exists = prepareSql(store_->sqlite_, "SELECT chain_id FROM plugin_chains WHERE chain_id=?");
        if (!exists.ok()) {
            return PersistenceResult<core::PluginChainDescriptor>::failure(exists.error);
        }
        (void)bindText(exists.value.handle, 1, chainId);
        const int existsResult = sqlite3_step(exists.value.handle);
        if (existsResult == SQLITE_DONE) {
            return PersistenceResult<core::PluginChainDescriptor>::failure(PersistenceError::NotFound);
        }
        if (existsResult != SQLITE_ROW) {
            return PersistenceResult<core::PluginChainDescriptor>::failure(sqliteError(existsResult));
        }
        core::PluginChainDescriptor chain;
        chain.identifier = chainId;
        auto slots = prepareSql(store_->sqlite_, "SELECT plugin_id, display_name, bypassed, parameter_state, latency_frames FROM plugin_chain_slots WHERE chain_id=? ORDER BY slot_index");
        if (!slots.ok()) {
            return PersistenceResult<core::PluginChainDescriptor>::failure(slots.error);
        }
        (void)bindText(slots.value.handle, 1, chainId);
        for (;;) {
            const int result = sqlite3_step(slots.value.handle);
            if (result == SQLITE_DONE) {
                return PersistenceResult<core::PluginChainDescriptor>::success(chain);
            }
            if (result != SQLITE_ROW) {
                return PersistenceResult<core::PluginChainDescriptor>::failure(sqliteError(result));
            }
            core::PluginDescriptor plugin{columnText(slots.value.handle, 0), columnText(slots.value.handle, 1), sqlite3_column_int(slots.value.handle, 2) != 0};
            plugin.parameters = deserializePluginParameters(columnText(slots.value.handle, 3));
            plugin.latencyFrames = static_cast<std::uint32_t>(std::max(0, sqlite3_column_int(slots.value.handle, 4)));
            chain.plugins.push_back(std::move(plugin));
        }
    }
#endif
    const auto found = store_->pluginChains_.find(chainId);
    return found == store_->pluginChains_.end() ? PersistenceResult<core::PluginChainDescriptor>::failure(PersistenceError::NotFound) : PersistenceResult<core::PluginChainDescriptor>::success(found->second);
}

AudioDevicePreferencesRepository::AudioDevicePreferencesRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult AudioDevicePreferencesRepository::save(AudioDevicePreferenceRecord record) {
    if (missingKey(record.deviceId)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO audio_device_preferences(device_id, display_name, sample_rate_hz, buffer_frames, degraded) VALUES(?, ?, ?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, record.deviceId);
        (void)bindText(statement.value.handle, 2, record.displayName);
        sqlite3_bind_int(statement.value.handle, 3, static_cast<int>(record.sampleRateHz));
        sqlite3_bind_int(statement.value.handle, 4, static_cast<int>(record.bufferFrames));
        sqlite3_bind_int(statement.value.handle, 5, record.degraded ? 1 : 0);
        return stepDone(statement.value.handle);
    }
#endif
    store_->audioDevicePreferences_[record.deviceId] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceResult<AudioDevicePreferenceRecord> AudioDevicePreferencesRepository::load(const std::string& deviceId) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT device_id, display_name, sample_rate_hz, buffer_frames, degraded FROM audio_device_preferences WHERE device_id=?");
        if (!statement.ok()) {
            return PersistenceResult<AudioDevicePreferenceRecord>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, deviceId);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<AudioDevicePreferenceRecord>::success(AudioDevicePreferenceRecord{columnText(statement.value.handle, 0), columnText(statement.value.handle, 1), static_cast<std::uint32_t>(sqlite3_column_int(statement.value.handle, 2)), static_cast<std::uint32_t>(sqlite3_column_int(statement.value.handle, 3)), sqlite3_column_int(statement.value.handle, 4) != 0});
        }
        return result == SQLITE_DONE ? PersistenceResult<AudioDevicePreferenceRecord>::failure(PersistenceError::NotFound) : PersistenceResult<AudioDevicePreferenceRecord>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->audioDevicePreferences_.find(deviceId);
    return found == store_->audioDevicePreferences_.end() ? PersistenceResult<AudioDevicePreferenceRecord>::failure(PersistenceError::NotFound) : PersistenceResult<AudioDevicePreferenceRecord>::success(found->second);
}

SandboxHealthRepository::SandboxHealthRepository(std::shared_ptr<InMemoryPersistenceStore> store) : store_(std::move(store)) {}

PersistenceUnitResult SandboxHealthRepository::save(SandboxHealthRecord record) {
    if (missingKey(record.component)) {
        return PersistenceUnitResult::failure(PersistenceError::InvalidRequest);
    }
    const auto writable = ensureWritable(*store_);
    if (!writable.ok()) {
        return writable;
    }
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "INSERT OR REPLACE INTO sandbox_health(component, healthy, detail) VALUES(?, ?, ?)");
        if (!statement.ok()) {
            return PersistenceUnitResult::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, record.component);
        sqlite3_bind_int(statement.value.handle, 2, record.healthy ? 1 : 0);
        (void)bindText(statement.value.handle, 3, record.detail);
        return stepDone(statement.value.handle);
    }
#endif
    store_->sandboxHealth_[record.component] = std::move(record);
    return PersistenceUnitResult::success();
}

PersistenceResult<SandboxHealthRecord> SandboxHealthRepository::load(const std::string& component) const {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    if (usesSQLite(*store_)) {
        auto statement = prepareSql(store_->sqlite_, "SELECT component, healthy, detail FROM sandbox_health WHERE component=?");
        if (!statement.ok()) {
            return PersistenceResult<SandboxHealthRecord>::failure(statement.error);
        }
        (void)bindText(statement.value.handle, 1, component);
        const int result = sqlite3_step(statement.value.handle);
        if (result == SQLITE_ROW) {
            return PersistenceResult<SandboxHealthRecord>::success(SandboxHealthRecord{columnText(statement.value.handle, 0), sqlite3_column_int(statement.value.handle, 1) != 0, columnText(statement.value.handle, 2)});
        }
        return result == SQLITE_DONE ? PersistenceResult<SandboxHealthRecord>::failure(PersistenceError::NotFound) : PersistenceResult<SandboxHealthRecord>::failure(sqliteError(result));
    }
#endif
    const auto found = store_->sandboxHealth_.find(component);
    return found == store_->sandboxHealth_.end() ? PersistenceResult<SandboxHealthRecord>::failure(PersistenceError::NotFound) : PersistenceResult<SandboxHealthRecord>::success(found->second);
}

PersistenceService::PersistenceService() : store_(std::make_shared<InMemoryPersistenceStore>()) {}

PersistenceService::PersistenceService(std::string sqlitePath) : store_(std::make_shared<InMemoryPersistenceStore>()) {
    store_->sqlite_ = std::make_shared<SQLiteConnection>();
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    sqlite3* database{};
    const int result = sqlite3_open_v2(sqlitePath.c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    store_->sqlite_->db = database;
    if (result == SQLITE_OK) {
        sqlite3_busy_timeout(database, 1);
        store_->sqlite_->open = true;
        store_->sqlite_->openError = PersistenceError::None;
    } else {
        store_->sqlite_->open = false;
        store_->sqlite_->openError = result == SQLITE_BUSY || result == SQLITE_LOCKED ? PersistenceError::DatabaseLocked : PersistenceError::OpenFailed;
    }
#else
    (void)sqlitePath;
    store_->sqlite_->open = false;
    store_->sqlite_->openError = PersistenceError::OpenFailed;
#endif
}

SQLiteIntegrationDecision PersistenceService::sqliteDecision() const noexcept {
    if (store_->sqlite_ != nullptr && store_->sqlite_->open) {
        return SQLiteIntegrationDecision{PersistenceBackendKind::SystemSQLiteCApi, true, "System SQLite C API is linked and selected for production persistence."};
    }
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
DeckStateRepository PersistenceService::deckStates() const { return DeckStateRepository{store_}; }
PluginChainsRepository PersistenceService::pluginChains() const { return PluginChainsRepository{store_}; }
AudioDevicePreferencesRepository PersistenceService::audioDevicePreferences() const { return AudioDevicePreferencesRepository{store_}; }
SandboxHealthRepository PersistenceService::sandboxHealth() const { return SandboxHealthRepository{store_}; }

}
