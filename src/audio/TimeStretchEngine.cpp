#include "audio/TimeStretchEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if DJAPP_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace djapp::audio {

namespace {

bool positiveFinite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

double boundedRate(double rate) noexcept {
    if (!positiveFinite(rate)) {
        return 1.0;
    }
    return std::clamp(rate, 0.25, 4.0);
}

TimeStretchStatus makeStatus(TimeStretchEngineKind kind,
                             TimeStretchProcessMode mode,
                             bool available,
                             std::uint32_t latencyFrames,
                             std::uint32_t preferredStartPadFrames,
                             std::uint32_t startDelayFrames,
                             std::uint32_t workerBufferFrames,
                             const TimeStretchSettings& settings) noexcept {
    TimeStretchStatus status;
    status.engine = kind;
    status.mode = mode;
    status.available = available;
    status.bypassed = settings.bypass || !available;
    status.fallback = kind != TimeStretchEngineKind::RubberBandRealTime;
    status.playbackRate = playbackRateForSettings(settings);
    status.effectiveTempoBpm = effectiveTempoForSettings(settings);
    status.pitchDriftCents = pitchDriftCentsForSettings(settings);
    status.latencyFrames = latencyFrames;
    status.preferredStartPadFrames = preferredStartPadFrames;
    status.startDelayFrames = startDelayFrames;
    status.workerBufferFrames = workerBufferFrames;
    return status;
}

}

TimeStretchEngineKind SignalsmithCompatibleFallbackTimeStretchEngine::kind() const noexcept {
    return TimeStretchEngineKind::SignalsmithCompatibleFallback;
}

bool SignalsmithCompatibleFallbackTimeStretchEngine::available() const noexcept {
    return true;
}

std::uint32_t SignalsmithCompatibleFallbackTimeStretchEngine::latencyFrames() const noexcept {
    return 0;
}

void SignalsmithCompatibleFallbackTimeStretchEngine::prepare(std::uint32_t sampleRateHz,
                                                             std::uint32_t maxBlockFrames,
                                                             const TimeStretchSettings&,
                                                             TimeStretchProcessMode mode) {
    sampleRateHz_ = sampleRateHz == 0U ? 44100U : sampleRateHz;
    maxBlockFrames_ = maxBlockFrames == 0U ? 512U : maxBlockFrames;
    mode_ = mode;
}

TimeStretchStatus SignalsmithCompatibleFallbackTimeStretchEngine::status(const TimeStretchSettings& settings) const noexcept {
    (void)sampleRateHz_;
    return makeStatus(kind(), mode_, true, latencyFrames(), 0U, 0U, maxBlockFrames_ * 2U, settings);
}

#if DJAPP_HAS_RUBBERBAND
struct RubberBandTimeStretchEngine::Impl final {
    RubberBand::RubberBandStretcher stretcher;

    Impl(std::uint32_t sampleRateHz, std::uint32_t channels, RubberBand::RubberBandStretcher::Options options)
        : stretcher(static_cast<size_t>(sampleRateHz), static_cast<size_t>(channels), options) {}
};

RubberBandTimeStretchEngine::RubberBandTimeStretchEngine() = default;

RubberBandTimeStretchEngine::~RubberBandTimeStretchEngine() {
    delete impl_;
}

TimeStretchEngineKind RubberBandTimeStretchEngine::kind() const noexcept {
    return TimeStretchEngineKind::RubberBandRealTime;
}

bool RubberBandTimeStretchEngine::available() const noexcept {
    return impl_ != nullptr;
}

std::uint32_t RubberBandTimeStretchEngine::latencyFrames() const noexcept {
    return latencyFrames_;
}

void RubberBandTimeStretchEngine::prepare(std::uint32_t sampleRateHz,
                                          std::uint32_t,
                                          const TimeStretchSettings& settings,
                                          TimeStretchProcessMode mode) {
    mode_ = mode;
    delete impl_;
    impl_ = nullptr;

    auto options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
    if (mode == TimeStretchProcessMode::OfflineHighQuality) {
        options = RubberBand::RubberBandStretcher::OptionProcessOffline |
                  RubberBand::RubberBandStretcher::OptionEngineFiner |
                  RubberBand::RubberBandStretcher::OptionPitchHighQuality;
    }
    impl_ = new RubberBandTimeStretchEngine::Impl(sampleRateHz == 0U ? 44100U : sampleRateHz, 2U, options);
    const auto playbackRate = playbackRateForSettings(settings);
    impl_->stretcher.setTimeRatio(playbackRate > 0.0 ? 1.0 / playbackRate : 1.0);
    impl_->stretcher.setPitchScale(std::pow(2.0, settings.pitchShiftCents / 1200.0));
    const auto latency = impl_->stretcher.getLatency();
    latencyFrames_ = static_cast<std::uint32_t>(std::min<size_t>(latency, std::numeric_limits<std::uint32_t>::max()));
    preferredStartPadFrames_ = static_cast<std::uint32_t>(std::min<size_t>(impl_->stretcher.getPreferredStartPad(), std::numeric_limits<std::uint32_t>::max()));
    startDelayFrames_ = static_cast<std::uint32_t>(std::min<size_t>(impl_->stretcher.getStartDelay(), std::numeric_limits<std::uint32_t>::max()));
}

TimeStretchStatus RubberBandTimeStretchEngine::status(const TimeStretchSettings& settings) const noexcept {
    return makeStatus(kind(), mode_, available(), latencyFrames_, preferredStartPadFrames_, startDelayFrames_, latencyFrames_ + preferredStartPadFrames_ + 1024U, settings);
}
#endif

bool rubberBandTimeStretchAvailable() noexcept {
#if DJAPP_HAS_RUBBERBAND
    return true;
#else
    return false;
#endif
}

TimeStretchEngineKind primaryTimeStretchEngineKind() noexcept {
#if DJAPP_HAS_RUBBERBAND
    return TimeStretchEngineKind::RubberBandRealTime;
#else
    return TimeStretchEngineKind::SignalsmithCompatibleFallback;
#endif
}

double playbackRateForSettings(const TimeStretchSettings& settings) noexcept {
    if (settings.bypass || !settings.tempoSyncEnabled) {
        return 1.0;
    }
    if (!positiveFinite(settings.sourceBpm) || !positiveFinite(settings.targetBpm)) {
        return 1.0;
    }
    return boundedRate(settings.targetBpm / settings.sourceBpm);
}

double effectiveTempoForSettings(const TimeStretchSettings& settings) noexcept {
    const auto source = positiveFinite(settings.sourceBpm) ? settings.sourceBpm : 120.0;
    return source * playbackRateForSettings(settings);
}

double pitchDriftCentsForSettings(const TimeStretchSettings& settings) noexcept {
    if (settings.bypass) {
        return 0.0;
    }
    if (settings.pitchLockEnabled) {
        return settings.pitchShiftCents;
    }
    return settings.pitchShiftCents + (1200.0 * std::log2(playbackRateForSettings(settings)));
}

const char* toString(TimeStretchEngineKind kind) noexcept {
    switch (kind) {
    case TimeStretchEngineKind::UnavailableBypass:
        return "unavailable-bypass";
    case TimeStretchEngineKind::SignalsmithCompatibleFallback:
        return "signalsmith-compatible-fallback";
    case TimeStretchEngineKind::RubberBandRealTime:
        return "rubber-band-real-time";
    }
    return "unavailable-bypass";
}

const char* toString(TimeStretchProcessMode mode) noexcept {
    switch (mode) {
    case TimeStretchProcessMode::LiveRealTime:
        return "live-real-time";
    case TimeStretchProcessMode::OfflineHighQuality:
        return "offline-high-quality";
    }
    return "live-real-time";
}

}
