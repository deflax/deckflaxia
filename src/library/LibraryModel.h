#pragma once

#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"
#include "persistence/Persistence.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace deckflaxia::library {

enum class LibraryError : std::uint8_t {
    None,
    WorkerUnavailable,
    RepositoryFailure,
    InvalidRequest,
};

struct LibraryUnitResult final {
    LibraryError error{LibraryError::None};

    [[nodiscard]] bool ok() const noexcept { return error == LibraryError::None; }
    [[nodiscard]] static constexpr LibraryUnitResult success() noexcept { return LibraryUnitResult{}; }
    [[nodiscard]] static constexpr LibraryUnitResult failure(LibraryError error) noexcept { return LibraryUnitResult{error}; }
};

template <typename T>
struct LibraryResult final {
    T value{};
    LibraryError error{LibraryError::None};

    [[nodiscard]] bool ok() const noexcept { return error == LibraryError::None; }
    [[nodiscard]] static LibraryResult success(T value) { return LibraryResult{std::move(value), LibraryError::None}; }
    [[nodiscard]] static LibraryResult failure(LibraryError error) { return LibraryResult{T{}, error}; }
};

enum class TrackAvailability : std::uint8_t {
    Available,
    Missing,
};

struct FilesystemEntry final {
    std::string path;
    bool regularFile{true};
};

struct WaveformCacheMetadata final {
    std::string trackId;
    std::size_t summaryPointCount{};
    double durationSeconds{};
};

struct BrowserTrackEntry final {
    core::LibraryTrack track;
    std::string path;
    TrackAvailability availability{TrackAvailability::Available};
    WaveformCacheMetadata waveform{};
};

struct FolderImportRequest final {
    std::uint64_t importId{};
    std::string crateId;
    std::string crateName;
    std::vector<FilesystemEntry> entries;
};

struct FolderImportResult final {
    std::vector<core::LibraryTrack> importedTracks;
    std::size_t skippedEntries{};
    bool cancelled{};
    LibraryError error{LibraryError::None};

    [[nodiscard]] bool ok() const noexcept { return error == LibraryError::None; }
};

class LibraryScanWorkerModel final : public core::CancellableBackgroundWorker {
public:
    [[nodiscard]] bool trySchedule(core::BackgroundJobTicket ticket) noexcept override;
    void requestStop() noexcept override;
    [[nodiscard]] bool stopRequested() const noexcept override;
    [[nodiscard]] bool scheduled() const noexcept;
    void requestStopAfterEntryCount(std::size_t entryCount) noexcept;
    [[nodiscard]] bool shouldStopAfterEntry(std::size_t processedEntryCount) const noexcept;

private:
    bool scheduled_{};
    bool stopRequested_{};
    std::size_t stopAfterEntryCount_{};
};

class ProLibraryRepository final {
public:
    ProLibraryRepository(persistence::LibraryTracksRepository tracks,
                         persistence::CratesRepository crates,
                         persistence::PlaylistsRepository playlists,
                         persistence::TrackMetadataRepository metadata);

    [[nodiscard]] FolderImportResult importFolderOnBackgroundWorker(const FolderImportRequest& request,
                                                                    core::BackgroundJobTicket ticket,
                                                                    LibraryScanWorkerModel& worker);
    [[nodiscard]] LibraryUnitResult createPlaylist(core::Playlist playlist);
    [[nodiscard]] LibraryUnitResult saveTrackMetadata(std::string trackId, core::BeatgridMetadata beatgrid, core::MusicalKey key);
    [[nodiscard]] LibraryUnitResult saveWaveformMetadata(WaveformCacheMetadata metadata);
    [[nodiscard]] LibraryUnitResult markTrackMissingByPath(const std::string& path);
    [[nodiscard]] LibraryResult<std::vector<BrowserTrackEntry>> browserTracks() const;
    [[nodiscard]] LibraryResult<BrowserTrackEntry> findBrowserTrack(const std::string& trackId) const;

private:
    persistence::LibraryTracksRepository tracks_;
    persistence::CratesRepository crates_;
    persistence::PlaylistsRepository playlists_;
    persistence::TrackMetadataRepository metadata_;
    std::map<std::string, std::string> trackPaths_;
    std::map<std::string, TrackAvailability> availability_;
    std::map<std::string, WaveformCacheMetadata> waveforms_;
};

[[nodiscard]] bool isSupportedAudioPath(const std::string& path) noexcept;
[[nodiscard]] std::string trackIdFromPath(const std::string& path);
[[nodiscard]] std::string titleFromPath(const std::string& path);

}
