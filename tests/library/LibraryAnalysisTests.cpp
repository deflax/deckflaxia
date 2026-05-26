#include "analysis/AnalysisPipeline.h"
#include "library/LibraryModel.h"
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

int expectOk(const djapp::library::FolderImportResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const djapp::library::LibraryUnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

template <typename T>
int expectOk(const djapp::library::LibraryResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const djapp::analysis::AnalysisRunResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

djapp::core::BackgroundJobTicket libraryTicket(std::uint64_t id) {
    return djapp::core::BackgroundJobTicket{id, djapp::core::BackgroundJobKind::PersistLibraryChange, djapp::core::BackgroundWorkerRole::DatabaseWorker};
}

djapp::core::BackgroundJobTicket analysisTicket(std::uint64_t id) {
    return djapp::core::BackgroundJobTicket{id, djapp::core::BackgroundJobKind::AnalyzeTrack, djapp::core::BackgroundWorkerRole::AnalysisPool};
}

djapp::library::ProLibraryRepository makeLibrary(djapp::persistence::PersistenceService& service) {
    return djapp::library::ProLibraryRepository{service.libraryTracks(), service.crates(), service.playlists(), service.trackMetadata()};
}

djapp::library::FolderImportRequest twoTrackImport() {
    return djapp::library::FolderImportRequest{1,
                                              "crate-alpha",
                                              "Alpha Crate",
                                              {djapp::library::FilesystemEntry{"/music/alpha.wav", true},
                                               djapp::library::FilesystemEntry{"/music/readme.txt", true},
                                               djapp::library::FilesystemEntry{"/music/beta.FLAC", true}}};
}

int testImportCrate() {
    using namespace djapp::core;
    using namespace djapp::library;
    using namespace djapp::persistence;

    PersistenceService service;
    ProLibraryRepository library = makeLibrary(service);
    LibraryScanWorkerModel worker;
    const auto import = library.importFolderOnBackgroundWorker(twoTrackImport(), libraryTicket(1), worker);
    if (expectOk(import, "library folder import") != 0) {
        return 1;
    }
    if (expect(worker.scheduled(), "folder import should schedule through database worker") != 0) {
        return 1;
    }
    if (expect(import.importedTracks.size() == 2 && import.skippedEntries == 1, "audio extension filter should import two tracks") != 0) {
        return 1;
    }
    if (expectOk(library.createPlaylist(Playlist{"playlist-alpha", "Set", {import.importedTracks[0].id}}), "playlist creation") != 0) {
        return 1;
    }

    const auto crate = service.crates().findById("crate-alpha");
    const auto playlist = service.playlists().findById("playlist-alpha");
    if (expect(crate.ok() && crate.value.trackIds.size() == 2, "crate should persist imported tracks") != 0 ||
        expect(playlist.ok() && playlist.value.trackIds.size() == 1, "playlist should persist selected track") != 0) {
        return 1;
    }

    ProLibraryRepository reloaded = makeLibrary(service);
    const auto persistedTracks = reloaded.browserTracks();
    if (expectOk(persistedTracks, "reloaded browser tracks") != 0 || expect(persistedTracks.value.size() == 2, "tracks should reload from persistence") != 0) {
        return 1;
    }

    std::cout << "Library.ImportCrate imported=" << import.importedTracks.size() << " crate=" << crate.value.trackIds.size() << '\n';
    return 0;
}

int testMetadataPersistence() {
    using namespace djapp::core;
    using namespace djapp::library;
    using namespace djapp::persistence;

    PersistenceService service;
    ProLibraryRepository library = makeLibrary(service);
    LibraryScanWorkerModel worker;
    const auto imported = library.importFolderOnBackgroundWorker(twoTrackImport(), libraryTicket(2), worker);
    if (expectOk(imported, "metadata import") != 0) {
        return 1;
    }

    const auto beatgrid = BeatgridMetadata::fromBpm(128.0, 0.125).value;
    const auto trackId = imported.importedTracks[0].id;
    if (expectOk(library.saveTrackMetadata(trackId, beatgrid, MusicalKey::Camelot8B), "beatgrid/key metadata save") != 0) {
        return 1;
    }
    if (expectOk(library.saveWaveformMetadata(WaveformCacheMetadata{trackId, 512, 240.0}), "waveform cache metadata save") != 0) {
        return 1;
    }

    const auto track = library.findBrowserTrack(trackId);
    if (expectOk(track, "browser track with metadata") != 0) {
        return 1;
    }
    if (expect(std::abs(track.value.track.beatgrid.bpm - 128.0) < 0.000001 && track.value.track.key == MusicalKey::Camelot8B,
               "browser model should expose persisted beatgrid/key") != 0) {
        return 1;
    }
    return expect(track.value.waveform.summaryPointCount == 512, "browser model should expose waveform cache metadata");
}

int testDeletedTrackHandling() {
    using namespace djapp::library;
    using namespace djapp::persistence;

    PersistenceService service;
    ProLibraryRepository library = makeLibrary(service);
    LibraryScanWorkerModel worker;
    const auto imported = library.importFolderOnBackgroundWorker(twoTrackImport(), libraryTicket(3), worker);
    if (expectOk(imported, "deleted-track import") != 0) {
        return 1;
    }
    if (expectOk(library.markTrackMissingByPath("/music/alpha.wav"), "deleted track missing mark") != 0) {
        return 1;
    }
    const auto track = library.findBrowserTrack(djapp::library::trackIdFromPath("/music/alpha.wav"));
    if (expectOk(track, "missing browser track") != 0) {
        return 1;
    }
    return expect(track.value.availability == TrackAvailability::Missing, "deleted track should be marked missing instead of removed");
}

int testHugeScanCancellation() {
    using namespace djapp::library;
    using namespace djapp::persistence;

    PersistenceService service;
    ProLibraryRepository library = makeLibrary(service);
    FolderImportRequest request{4, "crate-huge", "Huge", {}};
    for (int index = 0; index < 100; ++index) {
        request.entries.push_back(FilesystemEntry{"/huge/track-" + std::to_string(index) + ".wav", true});
    }

    LibraryScanWorkerModel worker;
    worker.requestStopAfterEntryCount(10);
    const auto imported = library.importFolderOnBackgroundWorker(request, libraryTicket(4), worker);
    if (expectOk(imported, "cancelled huge scan") != 0) {
        return 1;
    }
    if (expect(imported.cancelled && imported.importedTracks.size() == 10, "huge scan should cancel between entries") != 0) {
        return 1;
    }
    const auto crate = service.crates().findById("crate-huge");
    return expect(crate.ok() && crate.value.trackIds.size() == imported.importedTracks.size(), "cancelled scan should leave crate consistent");
}

int testAnalysisResumeJob() {
    using namespace djapp::analysis;
    using namespace djapp::core;
    using namespace djapp::library;
    using namespace djapp::persistence;

    PersistenceService service;
    ProLibraryRepository library = makeLibrary(service);
    LibraryScanWorkerModel scanWorker;
    const auto imported = library.importFolderOnBackgroundWorker(twoTrackImport(), libraryTicket(5), scanWorker);
    if (expectOk(imported, "analysis import") != 0) {
        return 1;
    }

    StubTrackAnalyzer analyzer;
    AnalysisJobQueue firstQueue(service.analysisJobs(), library, analyzer);
    AnalysisWorkerModel interruptedWorker;
    interruptedWorker.requestStopBeforeCompletion();
    const auto interrupted = firstQueue.enqueueAndRun(imported.importedTracks[0], analysisTicket(5), interruptedWorker);
    if (expectOk(interrupted, "interrupted analysis run") != 0) {
        return 1;
    }
    if (expect(interrupted.job.status == AnalysisJobStatus::Queued && interruptedWorker.stopRequested(), "interrupted job should remain resumable") != 0) {
        return 1;
    }

    AnalysisJobQueue resumedQueue(service.analysisJobs(), library, analyzer);
    AnalysisWorkerModel resumeWorker;
    const auto resumed = resumedQueue.resumeNextInterrupted(analysisTicket(6), resumeWorker);
    if (expectOk(resumed, "resumed analysis run") != 0) {
        return 1;
    }
    if (expect(resumed.job.status == AnalysisJobStatus::Complete && std::abs(resumed.job.progress - 1.0) < 0.000001,
               "resumed job should complete") != 0) {
        return 1;
    }
    const auto analyzedTrack = library.findBrowserTrack(imported.importedTracks[0].id);
    if (expectOk(analyzedTrack, "analyzed browser track") != 0) {
        return 1;
    }
    if (expect(analyzedTrack.value.track.key == MusicalKey::Camelot8A && analyzedTrack.value.waveform.summaryPointCount > 0,
               "analysis should persist key and waveform metadata") != 0) {
        return 1;
    }

    std::cout << "Analysis.ResumeJob status=complete waveform=" << resumed.waveform.summaryPointCount << '\n';
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "import-crate") {
        return testImportCrate();
    }
    if (filter == "metadata") {
        return testMetadataPersistence();
    }
    if (filter == "deleted") {
        return testDeletedTrackHandling();
    }
    if (filter == "cancel") {
        return testHugeScanCancellation();
    }
    if (filter == "analysis-resume") {
        return testAnalysisResumeJob();
    }

    if (testImportCrate() != 0 || testMetadataPersistence() != 0 || testDeletedTrackHandling() != 0 || testHugeScanCancellation() != 0 ||
        testAnalysisResumeJob() != 0) {
        return 1;
    }

    std::cout << "Library and analysis tests passed\n";
    return 0;
}
