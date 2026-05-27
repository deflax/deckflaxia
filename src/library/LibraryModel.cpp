#include "library/LibraryModel.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace deckflaxia::library {

namespace {

bool persistenceOk(persistence::PersistenceUnitResult result) noexcept {
    return result.ok();
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

core::BeatgridMetadata defaultBeatgrid() {
    return core::BeatgridMetadata::fromBpm(120.0, 0.0).value;
}

}

bool LibraryScanWorkerModel::trySchedule(core::BackgroundJobTicket ticket) noexcept {
    if (ticket.kind != core::BackgroundJobKind::PersistLibraryChange || ticket.role != core::BackgroundWorkerRole::DatabaseWorker) {
        return false;
    }
    scheduled_ = true;
    stopRequested_ = false;
    return true;
}

void LibraryScanWorkerModel::requestStop() noexcept {
    stopRequested_ = true;
}

bool LibraryScanWorkerModel::stopRequested() const noexcept {
    return stopRequested_;
}

bool LibraryScanWorkerModel::scheduled() const noexcept {
    return scheduled_;
}

void LibraryScanWorkerModel::requestStopAfterEntryCount(std::size_t entryCount) noexcept {
    stopAfterEntryCount_ = entryCount;
}

bool LibraryScanWorkerModel::shouldStopAfterEntry(std::size_t processedEntryCount) const noexcept {
    return stopAfterEntryCount_ > 0U && processedEntryCount >= stopAfterEntryCount_;
}

ProLibraryRepository::ProLibraryRepository(persistence::LibraryTracksRepository tracks,
                                           persistence::CratesRepository crates,
                                           persistence::PlaylistsRepository playlists,
                                           persistence::TrackMetadataRepository metadata)
    : tracks_(std::move(tracks)), crates_(std::move(crates)), playlists_(std::move(playlists)), metadata_(std::move(metadata)) {}

FolderImportResult ProLibraryRepository::importFolderOnBackgroundWorker(const FolderImportRequest& request,
                                                                        core::BackgroundJobTicket ticket,
                                                                        LibraryScanWorkerModel& worker) {
    FolderImportResult result;
    if (!worker.trySchedule(ticket)) {
        result.error = LibraryError::WorkerUnavailable;
        return result;
    }

    core::Crate crate{request.crateId, request.crateName, {}};
    std::size_t processedEntryCount{};
    for (const auto& entry : request.entries) {
        if (worker.stopRequested()) {
            result.cancelled = true;
            break;
        }
        ++processedEntryCount;
        if (!entry.regularFile || !isSupportedAudioPath(entry.path)) {
            ++result.skippedEntries;
            if (worker.shouldStopAfterEntry(processedEntryCount)) {
                worker.requestStop();
            }
            continue;
        }

        const auto trackId = trackIdFromPath(entry.path);
        core::LibraryTrack track{trackId, titleFromPath(entry.path), "", defaultBeatgrid(), core::MusicalKey::Unknown};
        if (!persistenceOk(tracks_.upsert(track))) {
            result.error = LibraryError::RepositoryFailure;
            return result;
        }
        trackPaths_[trackId] = entry.path;
        availability_[trackId] = TrackAvailability::Available;
        crate.trackIds.push_back(trackId);
        result.importedTracks.push_back(track);

        if (worker.shouldStopAfterEntry(processedEntryCount)) {
            worker.requestStop();
        }
    }

    if (!crate.id.empty() && !persistenceOk(crates_.save(crate))) {
        result.error = LibraryError::RepositoryFailure;
        return result;
    }
    return result;
}

LibraryUnitResult ProLibraryRepository::createPlaylist(core::Playlist playlist) {
    return persistenceOk(playlists_.save(std::move(playlist))) ? LibraryUnitResult::success() : LibraryUnitResult::failure(LibraryError::RepositoryFailure);
}

LibraryUnitResult ProLibraryRepository::saveTrackMetadata(std::string trackId, core::BeatgridMetadata beatgrid, core::MusicalKey key) {
    const auto track = tracks_.findById(trackId);
    if (!track.ok()) {
        return LibraryUnitResult::failure(LibraryError::InvalidRequest);
    }
    auto updatedTrack = track.value;
    updatedTrack.beatgrid = beatgrid;
    updatedTrack.key = key;
    if (!persistenceOk(tracks_.upsert(updatedTrack)) || !persistenceOk(metadata_.save(persistence::TrackMetadataRecord{std::move(trackId), beatgrid, key}))) {
        return LibraryUnitResult::failure(LibraryError::RepositoryFailure);
    }
    return LibraryUnitResult::success();
}

LibraryUnitResult ProLibraryRepository::saveWaveformMetadata(WaveformCacheMetadata metadata) {
    if (metadata.trackId.empty()) {
        return LibraryUnitResult::failure(LibraryError::InvalidRequest);
    }
    waveforms_[metadata.trackId] = std::move(metadata);
    return LibraryUnitResult::success();
}

LibraryUnitResult ProLibraryRepository::markTrackMissingByPath(const std::string& path) {
    for (const auto& entry : trackPaths_) {
        if (entry.second == path) {
            availability_[entry.first] = TrackAvailability::Missing;
            return LibraryUnitResult::success();
        }
    }
    return LibraryUnitResult::failure(LibraryError::InvalidRequest);
}

LibraryResult<std::vector<BrowserTrackEntry>> ProLibraryRepository::browserTracks() const {
    const auto tracks = tracks_.list();
    if (!tracks.ok()) {
        return LibraryResult<std::vector<BrowserTrackEntry>>::failure(LibraryError::RepositoryFailure);
    }

    std::vector<BrowserTrackEntry> entries;
    entries.reserve(tracks.value.size());
    for (auto track : tracks.value) {
        const auto metadata = metadata_.load(track.id);
        if (metadata.ok()) {
            track.beatgrid = metadata.value.beatgrid;
            track.key = metadata.value.key;
        }
        BrowserTrackEntry browserEntry;
        browserEntry.track = track;
        const auto path = trackPaths_.find(track.id);
        if (path != trackPaths_.end()) {
            browserEntry.path = path->second;
        }
        const auto availability = availability_.find(track.id);
        browserEntry.availability = availability == availability_.end() ? TrackAvailability::Available : availability->second;
        const auto waveform = waveforms_.find(track.id);
        if (waveform != waveforms_.end()) {
            browserEntry.waveform = waveform->second;
        }
        entries.push_back(browserEntry);
    }
    return LibraryResult<std::vector<BrowserTrackEntry>>::success(entries);
}

LibraryResult<BrowserTrackEntry> ProLibraryRepository::findBrowserTrack(const std::string& trackId) const {
    const auto entries = browserTracks();
    if (!entries.ok()) {
        return LibraryResult<BrowserTrackEntry>::failure(entries.error);
    }
    const auto found = std::find_if(entries.value.begin(), entries.value.end(), [&](const BrowserTrackEntry& entry) {
        return entry.track.id == trackId;
    });
    if (found == entries.value.end()) {
        return LibraryResult<BrowserTrackEntry>::failure(LibraryError::InvalidRequest);
    }
    return LibraryResult<BrowserTrackEntry>::success(*found);
}

bool isSupportedAudioPath(const std::string& path) noexcept {
    const auto lower = lowerCopy(path);
    return endsWith(lower, ".wav") || endsWith(lower, ".aiff") || endsWith(lower, ".aif") || endsWith(lower, ".flac") || endsWith(lower, ".mp3");
}

std::string trackIdFromPath(const std::string& path) {
    return std::string{"track:"} + path;
}

std::string titleFromPath(const std::string& path) {
    const auto separator = path.find_last_of("/");
    const auto fileName = separator == std::string::npos ? path : path.substr(separator + 1U);
    const auto extension = fileName.find_last_of('.');
    return extension == std::string::npos ? fileName : fileName.substr(0, extension);
}

}
