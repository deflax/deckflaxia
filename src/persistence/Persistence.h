#pragma once

#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace djapp::persistence {

constexpr int kCurrentSchemaVersion = 1;

enum class PersistenceBackendKind : std::uint8_t {
    InMemoryAlpha,
    SystemSQLiteCApi,
};

struct SQLiteIntegrationDecision final {
    PersistenceBackendKind selectedBackend{PersistenceBackendKind::InMemoryAlpha};
    bool commercialCompatible{true};
    const char* summary{"System SQLite C API is the commercial-compatible target; this build uses the explicit in-memory alpha backend because SQLite is not linkable."};
};

enum class PersistenceError : std::uint8_t {
    None,
    DatabaseLocked,
    MigrationFailed,
    InvalidSchema,
    OpenFailed,
    NotFound,
    InvalidRequest,
    WorkerUnavailable,
};

[[nodiscard]] bool isRecoverable(PersistenceError error) noexcept;

struct PersistenceUnitResult final {
    PersistenceError error{PersistenceError::None};

    [[nodiscard]] bool ok() const noexcept { return error == PersistenceError::None; }
    [[nodiscard]] static constexpr PersistenceUnitResult success() noexcept { return PersistenceUnitResult{}; }
    [[nodiscard]] static constexpr PersistenceUnitResult failure(PersistenceError error) noexcept { return PersistenceUnitResult{error}; }
};

template <typename T>
struct PersistenceResult final {
    T value{};
    PersistenceError error{PersistenceError::None};

    [[nodiscard]] bool ok() const noexcept { return error == PersistenceError::None; }
    [[nodiscard]] static PersistenceResult success(T value) { return PersistenceResult{std::move(value), PersistenceError::None}; }
    [[nodiscard]] static PersistenceResult failure(PersistenceError error) { return PersistenceResult{T{}, error}; }
};

struct AppPreference final {
    std::string key;
    std::string value;
};

struct RoutingConfigRecord final {
    std::size_t deckIndex{};
    core::RoutingAssignment assignment{};
};

struct PluginScanCacheRecord final {
    std::string pluginId;
    std::string displayName;
    std::string path;
    bool blacklisted{false};
};

struct TrackMetadataRecord final {
    std::string trackId;
    core::BeatgridMetadata beatgrid{};
    core::MusicalKey key{core::MusicalKey::Unknown};
};

struct CueMarkerRecord final {
    std::string id;
    double seconds{};
    std::string label;
};

struct DeckStateRecord final {
    std::size_t deckIndex{};
    core::DeckType type{core::DeckType::AudioFile};
    core::RoutingAssignment routing{};
    core::TransportState transport{};
    std::string loadedTrackId;
    core::TempoPitchSettings tempoPitch{};
};

struct AudioDevicePreferenceRecord final {
    std::string deviceId;
    std::string displayName;
    std::uint32_t sampleRateHz{};
    std::uint32_t bufferFrames{};
    bool degraded{};
};

struct SandboxHealthRecord final {
    std::string component;
    bool healthy{true};
    std::string detail;
};

struct SQLiteConnection;

class InMemoryPersistenceStore final {
public:
    [[nodiscard]] int schemaVersion() const noexcept;
    void setLockedForTest(bool locked) noexcept;
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] bool sqliteOpen() const noexcept;
    [[nodiscard]] PersistenceError sqliteOpenError() const noexcept;

private:
    friend class MigrationRunner;
    friend class AppPreferencesRepository;
    friend class RoutingConfigRepository;
    friend class PluginScanCacheRepository;
    friend class MidiMappingsRepository;
    friend class LibraryTracksRepository;
    friend class CratesRepository;
    friend class PlaylistsRepository;
    friend class TrackMetadataRepository;
    friend class AnalysisJobsRepository;
    friend class DeckStateRepository;
    friend class PluginChainsRepository;
    friend class AudioDevicePreferencesRepository;
    friend class SandboxHealthRepository;
    friend class PersistenceService;

    int schemaVersion_{};
    bool locked_{};
    std::shared_ptr<SQLiteConnection> sqlite_{};
    std::map<std::string, std::string> preferences_;
    std::map<std::size_t, core::RoutingAssignment> routing_;
    std::map<std::string, PluginScanCacheRecord> plugins_;
    std::map<std::string, core::MidiLearnMapping> midiMappings_;
    std::map<std::string, core::LibraryTrack> tracks_;
    std::map<std::string, core::Crate> crates_;
    std::map<std::string, core::Playlist> playlists_;
    std::map<std::string, TrackMetadataRecord> metadata_;
    std::map<std::string, std::vector<CueMarkerRecord>> cueMarkers_;
    std::map<std::string, core::AnalysisJob> analysisJobs_;
    std::map<std::size_t, DeckStateRecord> deckStates_;
    std::map<std::string, core::PluginChainDescriptor> pluginChains_;
    std::map<std::string, AudioDevicePreferenceRecord> audioDevicePreferences_;
    std::map<std::string, SandboxHealthRecord> sandboxHealth_;
};

class MigrationRunner final {
public:
    [[nodiscard]] PersistenceUnitResult migrateToCurrent(InMemoryPersistenceStore& store) const;
    [[nodiscard]] PersistenceUnitResult runFailingMigrationForTest(InMemoryPersistenceStore& store) const;
};

class DatabaseWorkerBoundary final {
public:
    [[nodiscard]] core::BackgroundWorkerRole role() const noexcept;
    [[nodiscard]] bool accepts(core::BackgroundJobTicket ticket) const noexcept;
    [[nodiscard]] PersistenceUnitResult runMigration(core::BackgroundJobTicket ticket, InMemoryPersistenceStore& store) const;
};

class AppPreferencesRepository final {
public:
    explicit AppPreferencesRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult put(AppPreference preference);
    [[nodiscard]] PersistenceResult<std::string> get(const std::string& key) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class RoutingConfigRepository final {
public:
    explicit RoutingConfigRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(RoutingConfigRecord record);
    [[nodiscard]] PersistenceResult<RoutingConfigRecord> load(std::size_t deckIndex) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class PluginScanCacheRepository final {
public:
    explicit PluginScanCacheRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult upsert(PluginScanCacheRecord record);
    [[nodiscard]] PersistenceUnitResult markBlacklisted(std::string pluginId, bool blacklisted);
    [[nodiscard]] PersistenceResult<std::vector<PluginScanCacheRecord>> list() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class MidiMappingsRepository final {
public:
    explicit MidiMappingsRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(core::MidiLearnMapping mapping);
    [[nodiscard]] PersistenceResult<std::vector<core::MidiLearnMapping>> list() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class LibraryTracksRepository final {
public:
    explicit LibraryTracksRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult upsert(core::LibraryTrack track);
    [[nodiscard]] PersistenceResult<core::LibraryTrack> findById(const std::string& id) const;
    [[nodiscard]] PersistenceResult<std::vector<core::LibraryTrack>> list() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class CratesRepository final {
public:
    explicit CratesRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(core::Crate crate);
    [[nodiscard]] PersistenceResult<core::Crate> findById(const std::string& id) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class PlaylistsRepository final {
public:
    explicit PlaylistsRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(core::Playlist playlist);
    [[nodiscard]] PersistenceResult<core::Playlist> findById(const std::string& id) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class TrackMetadataRepository final {
public:
    explicit TrackMetadataRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(TrackMetadataRecord record);
    [[nodiscard]] PersistenceResult<TrackMetadataRecord> load(const std::string& trackId) const;
    [[nodiscard]] PersistenceUnitResult saveCueMarkers(const std::string& trackId, std::vector<CueMarkerRecord> markers);
    [[nodiscard]] PersistenceResult<std::vector<CueMarkerRecord>> loadCueMarkers(const std::string& trackId) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class AnalysisJobsRepository final {
public:
    explicit AnalysisJobsRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult upsert(core::AnalysisJob job);
    [[nodiscard]] PersistenceResult<core::AnalysisJob> findById(const std::string& id) const;
    [[nodiscard]] PersistenceResult<std::vector<core::AnalysisJob>> list() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class DeckStateRepository final {
public:
    explicit DeckStateRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(DeckStateRecord record);
    [[nodiscard]] PersistenceResult<DeckStateRecord> load(std::size_t deckIndex) const;
    [[nodiscard]] PersistenceResult<std::vector<DeckStateRecord>> list() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class PluginChainsRepository final {
public:
    explicit PluginChainsRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(core::PluginChainDescriptor chain);
    [[nodiscard]] PersistenceResult<core::PluginChainDescriptor> load(const std::string& chainId) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class AudioDevicePreferencesRepository final {
public:
    explicit AudioDevicePreferencesRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(AudioDevicePreferenceRecord record);
    [[nodiscard]] PersistenceResult<AudioDevicePreferenceRecord> load(const std::string& deviceId) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class SandboxHealthRepository final {
public:
    explicit SandboxHealthRepository(std::shared_ptr<InMemoryPersistenceStore> store);
    [[nodiscard]] PersistenceUnitResult save(SandboxHealthRecord record);
    [[nodiscard]] PersistenceResult<SandboxHealthRecord> load(const std::string& component) const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
};

class PersistenceService final {
public:
    PersistenceService();
    explicit PersistenceService(std::string sqlitePath);

    [[nodiscard]] SQLiteIntegrationDecision sqliteDecision() const noexcept;
    [[nodiscard]] InMemoryPersistenceStore& store() noexcept;
    [[nodiscard]] const InMemoryPersistenceStore& store() const noexcept;
    [[nodiscard]] PersistenceUnitResult migrateOnDatabaseWorker(core::BackgroundJobTicket ticket);

    [[nodiscard]] AppPreferencesRepository appPreferences() const;
    [[nodiscard]] RoutingConfigRepository routingConfig() const;
    [[nodiscard]] PluginScanCacheRepository pluginScanCache() const;
    [[nodiscard]] MidiMappingsRepository midiMappings() const;
    [[nodiscard]] LibraryTracksRepository libraryTracks() const;
    [[nodiscard]] CratesRepository crates() const;
    [[nodiscard]] PlaylistsRepository playlists() const;
    [[nodiscard]] TrackMetadataRepository trackMetadata() const;
    [[nodiscard]] AnalysisJobsRepository analysisJobs() const;
    [[nodiscard]] DeckStateRepository deckStates() const;
    [[nodiscard]] PluginChainsRepository pluginChains() const;
    [[nodiscard]] AudioDevicePreferencesRepository audioDevicePreferences() const;
    [[nodiscard]] SandboxHealthRepository sandboxHealth() const;

private:
    std::shared_ptr<InMemoryPersistenceStore> store_;
    MigrationRunner migrations_;
    DatabaseWorkerBoundary worker_;
};

}
