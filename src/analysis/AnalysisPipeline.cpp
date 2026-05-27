#include "analysis/AnalysisPipeline.h"

#include <utility>

namespace deckflaxia::analysis {

namespace {

bool persistenceOk(persistence::PersistenceUnitResult result) noexcept {
    return result.ok();
}

}

TrackAnalysisResult StubTrackAnalyzer::analyze(const core::LibraryTrack& track) const {
    TrackAnalysisResult result;
    result.beatgrid = core::BeatgridMetadata::fromBpm(track.title.empty() ? 120.0 : 124.0, 0.0).value;
    result.key = core::MusicalKey::Camelot8A;
    result.waveform = library::WaveformCacheMetadata{track.id, track.title.size() + 8U, 180.0};
    return result;
}

bool AnalysisWorkerModel::trySchedule(core::BackgroundJobTicket ticket) noexcept {
    if (ticket.kind != core::BackgroundJobKind::AnalyzeTrack || ticket.role != core::BackgroundWorkerRole::AnalysisPool) {
        return false;
    }
    scheduled_ = true;
    stopRequested_ = false;
    return true;
}

void AnalysisWorkerModel::requestStop() noexcept {
    stopRequested_ = true;
}

bool AnalysisWorkerModel::stopRequested() const noexcept {
    return stopRequested_;
}

bool AnalysisWorkerModel::scheduled() const noexcept {
    return scheduled_;
}

void AnalysisWorkerModel::requestStopBeforeCompletion() noexcept {
    stopBeforeCompletion_ = true;
}

bool AnalysisWorkerModel::shouldStopBeforeCompletion() const noexcept {
    return stopBeforeCompletion_;
}

AnalysisJobQueue::AnalysisJobQueue(persistence::AnalysisJobsRepository jobs, library::ProLibraryRepository& library, const TrackAnalyzer& analyzer)
    : jobs_(std::move(jobs)), library_(library), analyzer_(analyzer) {}

AnalysisRunResult AnalysisJobQueue::enqueueAndRun(core::LibraryTrack track,
                                                  core::BackgroundJobTicket ticket,
                                                  AnalysisWorkerModel& worker) {
    auto job = core::AnalysisJob::queued(analysisJobIdForTrack(track.id), track.id);
    if (!persistenceOk(jobs_.upsert(job))) {
        return AnalysisRunResult{job, {}, AnalysisError::RepositoryFailure};
    }
    return runJob(std::move(job), std::move(track), ticket, worker);
}

AnalysisRunResult AnalysisJobQueue::resumeNextInterrupted(core::BackgroundJobTicket ticket, AnalysisWorkerModel& worker) {
    const auto jobs = jobs_.list();
    if (!jobs.ok()) {
        return AnalysisRunResult{{}, {}, AnalysisError::RepositoryFailure};
    }
    for (const auto& job : jobs.value) {
        if (job.status == core::AnalysisJobStatus::Queued || job.status == core::AnalysisJobStatus::Running) {
            const auto track = library_.findBrowserTrack(job.trackId);
            if (!track.ok()) {
                return AnalysisRunResult{job, {}, AnalysisError::RepositoryFailure};
            }
            return runJob(job, track.value.track, ticket, worker);
        }
    }
    return AnalysisRunResult{{}, {}, AnalysisError::RepositoryFailure};
}

AnalysisRunResult AnalysisJobQueue::runJob(core::AnalysisJob job,
                                           core::LibraryTrack track,
                                           core::BackgroundJobTicket ticket,
                                           AnalysisWorkerModel& worker) {
    if (!worker.trySchedule(ticket)) {
        return AnalysisRunResult{job, {}, AnalysisError::WorkerUnavailable};
    }

    if (!persistenceOk(jobs_.upsert(job))) {
        return AnalysisRunResult{job, {}, AnalysisError::RepositoryFailure};
    }
    if (worker.stopRequested() || worker.shouldStopBeforeCompletion()) {
        worker.requestStop();
        return AnalysisRunResult{job, {}, AnalysisError::None};
    }

    auto analysis = analyzer_.analyze(track);
    if (!library_.saveTrackMetadata(track.id, analysis.beatgrid, analysis.key).ok() || !library_.saveWaveformMetadata(analysis.waveform).ok()) {
        job.status = core::AnalysisJobStatus::Failed;
        const auto ignoredSave = jobs_.upsert(job);
        static_cast<void>(ignoredSave);
        return AnalysisRunResult{job, analysis.waveform, AnalysisError::RepositoryFailure};
    }
    if (!job.updateProgress(1.0).ok() || !persistenceOk(jobs_.upsert(job))) {
        return AnalysisRunResult{job, analysis.waveform, AnalysisError::RepositoryFailure};
    }
    return AnalysisRunResult{job, analysis.waveform, AnalysisError::None};
}

std::string analysisJobIdForTrack(const std::string& trackId) {
    return std::string{"analysis:"} + trackId;
}

}
