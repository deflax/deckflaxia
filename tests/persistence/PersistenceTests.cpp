#include "library/LibraryPersistence.h"
#include "persistence/Persistence.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int expectOk(const djapp::persistence::PersistenceUnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const djapp::core::UnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

template <typename T>
int expectOk(const djapp::persistence::PersistenceResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectError(const djapp::persistence::PersistenceUnitResult& result,
                djapp::persistence::PersistenceError error,
                const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == error, message + " should expose typed error");
}

int testMigration() {
    using namespace djapp::core;
    using namespace djapp::persistence;

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
    using namespace djapp::core;
    using namespace djapp::persistence;

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
    using namespace djapp::persistence;

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
    using namespace djapp::core;
    using namespace djapp::persistence;

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
    djapp::library::LibraryTracksRepository libraryTracks = service.libraryTracks();
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

    if (testMigration() != 0 || testLockedDatabase() != 0 || testFailedMigration() != 0 || testRepositories() != 0) {
        return 1;
    }

    std::cout << "Persistence tests passed\n";
    return 0;
}
