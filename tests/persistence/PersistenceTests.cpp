#include "library/LibraryPersistence.h"
#include "persistence/Persistence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
#include <sqlite3.h>
#endif

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int expectOk(const deckflaxia::persistence::PersistenceUnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const deckflaxia::core::UnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

template <typename T>
int expectOk(const deckflaxia::persistence::PersistenceResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectError(const deckflaxia::persistence::PersistenceUnitResult& result,
                deckflaxia::persistence::PersistenceError error,
                const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == error, message + " should expose typed error");
}

int testMigration() {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;

    PersistenceService service;
    const auto decision = service.sqliteDecision();
    if (expect(decision.commercialCompatible, "SQLite decision should be commercial-compatible") != 0) {
        return 1;
    }
    if (expect(decision.selectedBackend == PersistenceBackendKind::InMemoryAlpha,
               "current environment should use explicit in-memory alpha backend") != 0) {
        return 1;
    }
    if (expect(service.store().schemaVersion() == 0, "empty store should start before migrations") != 0) {
        return 1;
    }

    const BackgroundJobTicket ticket{7, BackgroundJobKind::PersistLibraryChange, BackgroundWorkerRole::DatabaseWorker};
    if (expectOk(service.migrateOnDatabaseWorker(ticket), "database worker migration") != 0) {
        return 1;
    }
    if (expect(service.store().schemaVersion() == kCurrentSchemaVersion, "migration should store current schema version") != 0) {
        return 1;
    }

    const BackgroundJobTicket wrongWorker{8, BackgroundJobKind::PersistLibraryChange, BackgroundWorkerRole::AnalysisPool};
    const auto wrongWorkerResult = service.migrateOnDatabaseWorker(wrongWorker);
    if (expectError(wrongWorkerResult, PersistenceError::WorkerUnavailable, "non-database worker migration") != 0) {
        return 1;
    }
    return expect(isRecoverable(wrongWorkerResult.error), "worker boundary failures should be recoverable");
}

int testLockedDatabase() {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;

    PersistenceService service;
    service.store().setLockedForTest(true);
    const BackgroundJobTicket ticket{9, BackgroundJobKind::PersistLibraryChange, BackgroundWorkerRole::DatabaseWorker};
    const auto result = service.migrateOnDatabaseWorker(ticket);
    if (expectError(result, PersistenceError::DatabaseLocked, "locked database migration") != 0) {
        return 1;
    }
    if (expect(isRecoverable(result.error), "locked database should be recoverable") != 0) {
        return 1;
    }
    return expect(service.store().schemaVersion() == 0, "locked database should not advance schema version");
}

int testFailedMigration() {
    using namespace deckflaxia::persistence;

    InMemoryPersistenceStore store;
    MigrationRunner migrations;
    const auto result = migrations.runFailingMigrationForTest(store);
    if (expectError(result, PersistenceError::MigrationFailed, "failing migration") != 0) {
        return 1;
    }
    if (expect(isRecoverable(result.error), "failed migration should be recoverable") != 0) {
        return 1;
    }
    return expect(store.schemaVersion() == 0, "failed migration should not write schema version");
}

int testRepositories() {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;

    PersistenceService service;
    const BackgroundJobTicket ticket{10, BackgroundJobKind::PersistLibraryChange, BackgroundWorkerRole::DatabaseWorker};
    if (expectOk(service.migrateOnDatabaseWorker(ticket), "repository test migration") != 0) {
        return 1;
    }

    if (expectOk(service.appPreferences().put(AppPreference{"audio.backend", "jack"}), "app preference save") != 0) {
        return 1;
    }
    const auto preference = service.appPreferences().get("audio.backend");
    if (expectOk(preference, "app preference load") != 0 || expect(preference.value == "jack", "app preference should round trip") != 0) {
        return 1;
    }

    const auto routing = RoutingAssignment::deckOutput(OutputBus::Output2, true).value;
    if (expectOk(service.routingConfig().save(RoutingConfigRecord{1, routing}), "routing config save") != 0) {
        return 1;
    }
    const auto routingResult = service.routingConfig().load(1);
    if (expectOk(routingResult, "routing config load") != 0 ||
        expect(routingResult.value.assignment.mainOutput == OutputBus::Output2 && routingResult.value.assignment.cueEnabled,
               "routing config should round trip") != 0) {
        return 1;
    }

    if (expectOk(service.pluginScanCache().upsert(PluginScanCacheRecord{"vst3.eq", "EQ", "/plugins/eq.vst3", false}),
                 "plugin cache save") != 0) {
        return 1;
    }
    if (expectOk(service.pluginScanCache().markBlacklisted("vst3.eq", true), "plugin blacklist update") != 0) {
        return 1;
    }
    const auto plugins = service.pluginScanCache().list();
    if (expectOk(plugins, "plugin cache list") != 0 || expect(plugins.value.size() == 1 && plugins.value[0].blacklisted,
                                                             "plugin cache should expose blacklist state") != 0) {
        return 1;
    }

    const auto midi = MidiLearnMapping::bind(MidiLearnTarget{"deck1.filter", "Deck 1 Filter"}, MidiMessageDescriptor{0, 74}).value;
    if (expectOk(service.midiMappings().save(midi), "MIDI mapping save") != 0) {
        return 1;
    }
    const auto midiMappings = service.midiMappings().list();
    if (expectOk(midiMappings, "MIDI mapping list") != 0 || expect(midiMappings.value.size() == 1,
                                                                    "MIDI mappings should round trip") != 0) {
        return 1;
    }

    const auto beatgrid = BeatgridMetadata::fromBpm(128.0, 0.25).value;
    const LibraryTrack track{"track-1", "Track", "Artist", beatgrid, MusicalKey::Camelot8A};
    deckflaxia::library::LibraryTracksRepository libraryTracks = service.libraryTracks();
    if (expectOk(libraryTracks.upsert(track), "library track save") != 0) {
        return 1;
    }
    const auto loadedTrack = libraryTracks.findById("track-1");
    if (expectOk(loadedTrack, "library track load") != 0 || expect(loadedTrack.value.title == "Track",
                                                                   "library track should round trip") != 0) {
        return 1;
    }

    if (expectOk(service.crates().save(Crate{"crate-1", "Crate", {"track-1"}}), "crate save") != 0) {
        return 1;
    }
    if (expectOk(service.playlists().save(Playlist{"playlist-1", "Playlist", {"track-1"}}), "playlist save") != 0) {
        return 1;
    }
    if (expectOk(service.crates().findById("crate-1"), "crate load") != 0 ||
        expectOk(service.playlists().findById("playlist-1"), "playlist load") != 0) {
        return 1;
    }

    if (expectOk(service.trackMetadata().save(TrackMetadataRecord{"track-1", beatgrid, MusicalKey::Camelot8B}),
                 "track metadata save") != 0) {
        return 1;
    }
    const auto metadata = service.trackMetadata().load("track-1");
    if (expectOk(metadata, "track metadata load") != 0 || expect(std::abs(metadata.value.beatgrid.bpm - 128.0) < 0.000001,
                                                                 "track metadata should round trip") != 0) {
        return 1;
    }

    auto job = AnalysisJob::queued("job-1", "track-1");
    if (expectOk(job.updateProgress(0.5), "analysis job progress update") != 0) {
        return 1;
    }
    if (expectOk(service.analysisJobs().upsert(job), "analysis job save") != 0) {
        return 1;
    }
    const auto jobs = service.analysisJobs().list();
    return expectOk(jobs, "analysis job list") != 0 ? 1 : expect(jobs.value.size() == 1 && jobs.value[0].status == AnalysisJobStatus::Running,
                                                                 "analysis jobs should round trip");
}

deckflaxia::core::BackgroundJobTicket databaseTicket() {
    return deckflaxia::core::BackgroundJobTicket{500, deckflaxia::core::BackgroundJobKind::PersistLibraryChange, deckflaxia::core::BackgroundWorkerRole::DatabaseWorker};
}

int importFixtureState(deckflaxia::persistence::PersistenceService& service) {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;

    const auto beatgrid = BeatgridMetadata::fromBpm(126.5, 0.125).value;
    const LibraryTrack track{"fixture-track", "Restart Fixture", "Persistence Artist", beatgrid, MusicalKey::Camelot8A};
    if (expectOk(service.libraryTracks().upsert(track), "sqlite track save") != 0) {
        return 1;
    }
    if (expectOk(service.crates().save(Crate{"crate-fixture", "Fixture Crate", {"fixture-track"}}), "sqlite crate save") != 0) {
        return 1;
    }
    if (expectOk(service.playlists().save(Playlist{"playlist-fixture", "Fixture Playlist", {"fixture-track"}}), "sqlite playlist save") != 0) {
        return 1;
    }
    if (expectOk(service.trackMetadata().save(TrackMetadataRecord{"fixture-track", beatgrid, MusicalKey::Camelot8B}), "sqlite beatgrid save") != 0) {
        return 1;
    }
    if (expectOk(service.deckStates().save(DeckStateRecord{2, DeckType::AudioFile, RoutingAssignment::deckOutput(OutputBus::Output3, true).value, TransportState{true, 64.5}, "fixture-track"}), "sqlite deck state save") != 0) {
        return 1;
    }
    if (expectOk(service.pluginScanCache().upsert(PluginScanCacheRecord{"vst3.fixture", "Fixture FX", "/plugins/fixture.vst3", false}), "sqlite plugin scan cache save") != 0) {
        return 1;
    }
    if (expectOk(service.pluginScanCache().upsert(PluginScanCacheRecord{"failed.bad", "failed plugin candidate", "/plugins/bad.txt", true}), "sqlite plugin blacklist save") != 0) {
        return 1;
    }
    if (expectOk(service.pluginChains().save(PluginChainDescriptor{"deck-3-chain", {PluginDescriptor{"vst3.fixture", "Fixture FX", false}}}), "sqlite plugin chain save") != 0) {
        return 1;
    }
    const auto midi = MidiLearnMapping::bind(MidiLearnTarget{"deck.3.transport.play", "Deck 3 Play"}, MidiMessageDescriptor{3, 45}).value;
    if (expectOk(service.midiMappings().save(midi), "sqlite MIDI mapping save") != 0) {
        return 1;
    }
    if (expectOk(service.audioDevicePreferences().save(AudioDevicePreferenceRecord{"default-output", "Default Output", 48000, 512, false}), "sqlite audio device preference save") != 0) {
        return 1;
    }
    if (expectOk(service.sandboxHealth().save(SandboxHealthRecord{"plugin-host", true, "scan completed"}), "sqlite sandbox health save") != 0) {
        return 1;
    }
    auto job = AnalysisJob::queued("analysis-fixture", "fixture-track");
    if (expectOk(job.updateProgress(1.0), "sqlite analysis progress") != 0) {
        return 1;
    }
    return expectOk(service.analysisJobs().upsert(job), "sqlite analysis job save");
}

int assertFixtureState(deckflaxia::persistence::PersistenceService& service) {
    using namespace deckflaxia::core;

    const auto track = service.libraryTracks().findById("fixture-track");
    if (expectOk(track, "sqlite track reload") != 0 || expect(track.value.title == "Restart Fixture" && std::abs(track.value.beatgrid.bpm - 126.5) < 0.000001, "sqlite track deterministic reload") != 0) {
        return 1;
    }
    const auto crate = service.crates().findById("crate-fixture");
    if (expectOk(crate, "sqlite crate reload") != 0 || expect(crate.value.trackIds.size() == 1 && crate.value.trackIds[0] == "fixture-track", "sqlite crate tracks reload") != 0) {
        return 1;
    }
    const auto playlist = service.playlists().findById("playlist-fixture");
    if (expectOk(playlist, "sqlite playlist reload") != 0 || expect(playlist.value.trackIds.size() == 1, "sqlite playlist tracks reload") != 0) {
        return 1;
    }
    const auto deck = service.deckStates().load(2);
    if (expectOk(deck, "sqlite deck reload") != 0 || expect(deck.value.transport.playing && deck.value.loadedTrackId == "fixture-track" && deck.value.routing.mainOutput == OutputBus::Output3, "sqlite deck state deterministic reload") != 0) {
        return 1;
    }
    const auto plugins = service.pluginScanCache().list();
    if (expectOk(plugins, "sqlite plugin cache reload") != 0 || expect(plugins.value.size() == 2, "sqlite plugin cache deterministic reload") != 0) {
        return 1;
    }
    const auto fixturePlugin = std::find_if(plugins.value.begin(), plugins.value.end(), [](const auto& plugin) {
        return plugin.pluginId == "vst3.fixture";
    });
    const auto failedPlugin = std::find_if(plugins.value.begin(), plugins.value.end(), [](const auto& plugin) {
        return plugin.pluginId == "failed.bad";
    });
    if (expect(fixturePlugin != plugins.value.end() && !fixturePlugin->blacklisted, "sqlite plugin cache should reload fixture plugin") != 0 ||
        expect(failedPlugin != plugins.value.end() && failedPlugin->blacklisted, "sqlite plugin cache should reload blacklisted plugin") != 0) {
        return 1;
    }
    const auto chain = service.pluginChains().load("deck-3-chain");
    if (expectOk(chain, "sqlite plugin chain reload") != 0 || expect(chain.value.plugins.size() == 1 && chain.value.plugins[0].identifier == "vst3.fixture", "sqlite plugin chain deterministic reload") != 0) {
        return 1;
    }
    const auto midi = service.midiMappings().list();
    if (expectOk(midi, "sqlite MIDI reload") != 0 || expect(midi.value.size() == 1 && midi.value[0].message.controller == 45, "sqlite MIDI deterministic reload") != 0) {
        return 1;
    }
    const auto device = service.audioDevicePreferences().load("default-output");
    if (expectOk(device, "sqlite audio device reload") != 0 || expect(device.value.sampleRateHz == 48000 && device.value.bufferFrames == 512, "sqlite audio device deterministic reload") != 0) {
        return 1;
    }
    const auto sandbox = service.sandboxHealth().load("plugin-host");
    if (expectOk(sandbox, "sqlite sandbox reload") != 0 || expect(sandbox.value.healthy && sandbox.value.detail == "scan completed", "sqlite sandbox deterministic reload") != 0) {
        return 1;
    }
    const auto metadata = service.trackMetadata().load("fixture-track");
    return expectOk(metadata, "sqlite beatgrid reload") != 0 ? 1 : expect(metadata.value.key == MusicalKey::Camelot8B, "sqlite beatgrid/key deterministic reload");
}

int testSQLiteRestartSmoke(const std::string& dbPath) {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    std::remove(dbPath.c_str());
    {
        deckflaxia::persistence::PersistenceService firstRun(dbPath);
        if (expect(firstRun.sqliteDecision().selectedBackend == deckflaxia::persistence::PersistenceBackendKind::SystemSQLiteCApi, "sqlite backend should be selected") != 0) {
            return 1;
        }
        if (expectOk(firstRun.migrateOnDatabaseWorker(databaseTicket()), "sqlite first migration") != 0) {
            return 1;
        }
        if (importFixtureState(firstRun) != 0) {
            return 1;
        }
    }
    {
        deckflaxia::persistence::PersistenceService secondRun(dbPath);
        if (expectOk(secondRun.migrateOnDatabaseWorker(databaseTicket()), "sqlite second migration") != 0) {
            return 1;
        }
        if (assertFixtureState(secondRun) != 0) {
            return 1;
        }
    }
    std::cout << "SQLite restart smoke passed db=" << dbPath << "\n";
    return 0;
#else
    std::cout << "SQLite unavailable; JUCE-required command shape: cmake -S . -B build -DDECKFLAXIA_REQUIRE_JUCE=ON -DCMAKE_PREFIX_PATH=/path/to/JUCE && cmake --build build && ctest --test-dir build -R SQLitePersistence --output-on-failure\n";
    (void)dbPath;
    return 0;
#endif
}

int testSQLiteMigrationFailure(const std::string& dbPath) {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    std::remove(dbPath.c_str());
    {
        std::ofstream invalid(dbPath, std::ios::binary);
        invalid << "not a sqlite database";
    }
    deckflaxia::persistence::PersistenceService service(dbPath);
    const auto result = service.migrateOnDatabaseWorker(databaseTicket());
    return expectError(result, deckflaxia::persistence::PersistenceError::MigrationFailed, "invalid sqlite migration") != 0 ? 1 : expect(result.error != deckflaxia::persistence::PersistenceError::None, "invalid sqlite should preserve typed failure");
#else
    (void)dbPath;
    std::cout << "SQLite unavailable; migration failure path covered by in-memory typed failure\n";
    return 0;
#endif
}

int testSQLiteLockedDatabase(const std::string& dbPath) {
#if DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE
    std::remove(dbPath.c_str());
    {
        deckflaxia::persistence::PersistenceService setup(dbPath);
        if (expectOk(setup.migrateOnDatabaseWorker(databaseTicket()), "sqlite setup migration") != 0) {
            return 1;
        }
    }
    sqlite3* rawDatabase{};
    if (sqlite3_open_v2(dbPath.c_str(), &rawDatabase, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        sqlite3_close(rawDatabase);
        return expect(false, "sqlite raw lock connection should open");
    }
    if (sqlite3_exec(rawDatabase, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(rawDatabase);
        return expect(false, "sqlite raw exclusive lock should start");
    }
    deckflaxia::persistence::PersistenceService locked(dbPath);
    const auto result = locked.migrateOnDatabaseWorker(databaseTicket());
    sqlite3_exec(rawDatabase, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_close(rawDatabase);
    return expectError(result, deckflaxia::persistence::PersistenceError::DatabaseLocked, "sqlite locked database") != 0 ? 1 : expect(deckflaxia::persistence::isRecoverable(result.error), "sqlite locked database should be recoverable");
#else
    (void)dbPath;
    std::cout << "SQLite unavailable; locked path covered by in-memory typed failure\n";
    return 0;
#endif
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "migrations") {
        return testMigration();
    }
    if (filter == "locked") {
        return testLockedDatabase();
    }
    if (filter == "failed") {
        return testFailedMigration();
    }
    if (filter == "repositories") {
        return testRepositories();
    }
    if (filter == "sqlite-restart") {
        return testSQLiteRestartSmoke(argc > 2 ? argv[2] : "sqlite-persistence-smoke.db");
    }
    if (filter == "sqlite-failure") {
        return testSQLiteMigrationFailure(argc > 2 ? argv[2] : "sqlite-persistence-invalid.db");
    }
    if (filter == "sqlite-locked") {
        return testSQLiteLockedDatabase(argc > 2 ? argv[2] : "sqlite-persistence-locked.db");
    }

    if (testMigration() != 0 || testLockedDatabase() != 0 || testFailedMigration() != 0 || testRepositories() != 0 ||
        testSQLiteRestartSmoke("sqlite-persistence-smoke.db") != 0 || testSQLiteMigrationFailure("sqlite-persistence-invalid.db") != 0 ||
        testSQLiteLockedDatabase("sqlite-persistence-locked.db") != 0) {
        return 1;
    }

    std::cout << "Persistence tests passed\n";
    return 0;
}
