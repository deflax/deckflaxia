#pragma once

#include "core/BackgroundWorkerContracts.h"
#include "core/DomainModels.h"
#include "library/LibraryModel.h"
#include "persistence/Persistence.h"

#include <cstdint>
#include <string>

namespace djapp::analysis {

enum class AnalysisError : std::uint8_t {
    None,
    WorkerUnavailable,
    RepositoryFailure,
    AnalyzerFailure,
};

struct AnalysisRunResult final {
    core::AnalysisJob job;
    library::WaveformCacheMetadata waveform;
    AnalysisError error{AnalysisError::None};

    [[nodiscard]] bool ok() const noexcept { return error == AnalysisError::None; }
};

struct TrackAnalysisResult final {
    core::BeatgridMetadata beatgrid{};
    core::MusicalKey key{core::MusicalKey::Unknown};
    library::WaveformCacheMetadata waveform{};
};

class TrackAnalyzer {
public:
    virtual ~TrackAnalyzer() = default;
    [[nodiscard]] virtual TrackAnalysisResult analyze(const core::LibraryTrack& track) const = 0;
};

class StubTrackAnalyzer final : public TrackAnalyzer {
public:
    [[nodiscard]] TrackAnalysisResult analyze(const core::LibraryTrack& track) const override;
};

class AnalysisWorkerModel final : public core::CancellableBackgroundWorker {
public:
    [[nodiscard]] bool trySchedule(core::BackgroundJobTicket ticket) noexcept override;
    void requestStop() noexcept override;
    [[nodiscard]] bool stopRequested() const noexcept override;
    [[nodiscard]] bool scheduled() const noexcept;
    void requestStopBeforeCompletion() noexcept;
    [[nodiscard]] bool shouldStopBeforeCompletion() const noexcept;

private:
    bool scheduled_{};
    bool stopRequested_{};
    bool stopBeforeCompletion_{};
};

class AnalysisJobQueue final {
public:
    AnalysisJobQueue(persistence::AnalysisJobsRepository jobs, library::ProLibraryRepository& library, const TrackAnalyzer& analyzer);

    [[nodiscard]] AnalysisRunResult enqueueAndRun(core::LibraryTrack track,
                                                  core::BackgroundJobTicket ticket,
                                                  AnalysisWorkerModel& worker);
    [[nodiscard]] AnalysisRunResult resumeNextInterrupted(core::BackgroundJobTicket ticket, AnalysisWorkerModel& worker);

private:
    [[nodiscard]] AnalysisRunResult runJob(core::AnalysisJob job,
                                           core::LibraryTrack track,
                                           core::BackgroundJobTicket ticket,
                                           AnalysisWorkerModel& worker);

    persistence::AnalysisJobsRepository jobs_;
    library::ProLibraryRepository& library_;
    const TrackAnalyzer& analyzer_;
};

[[nodiscard]] std::string analysisJobIdForTrack(const std::string& trackId);

}
