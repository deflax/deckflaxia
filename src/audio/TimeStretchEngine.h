#pragma once

#include <cstdint>

#ifndef DJAPP_HAS_RUBBERBAND
#define DJAPP_HAS_RUBBERBAND 0
#endif

namespace djapp::audio {

enum class TimeStretchEngineKind : std::uint8_t {
    UnavailableBypass,
    SignalsmithCompatibleFallback,
    RubberBandRealTime,
};

enum class TimeStretchProcessMode : std::uint8_t {
    LiveRealTime,
    OfflineHighQuality,
};

struct TimeStretchSettings final {
    double sourceBpm{120.0};
    double targetBpm{120.0};
    bool tempoSyncEnabled{false};
    bool pitchLockEnabled{true};
    double pitchShiftCents{0.0};
    bool bypass{false};
};

struct TimeStretchStatus final {
    TimeStretchEngineKind engine{TimeStretchEngineKind::SignalsmithCompatibleFallback};
    TimeStretchProcessMode mode{TimeStretchProcessMode::LiveRealTime};
    bool available{true};
    bool bypassed{false};
    bool fallback{true};
    double playbackRate{1.0};
    double effectiveTempoBpm{120.0};
    double pitchDriftCents{0.0};
    std::uint32_t latencyFrames{};
    std::uint32_t preferredStartPadFrames{};
    std::uint32_t startDelayFrames{};
    std::uint32_t workerBufferFrames{};
    std::uint64_t inputFramesConsumed{};
    std::uint64_t underrunFrames{};
    std::uint64_t fallbackEvents{};
};

class TimeStretchEngine {
public:
    virtual ~TimeStretchEngine() = default;
    [[nodiscard]] virtual TimeStretchEngineKind kind() const noexcept = 0;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t latencyFrames() const noexcept = 0;
    virtual void prepare(std::uint32_t sampleRateHz,
                         std::uint32_t maxBlockFrames,
                         const TimeStretchSettings& settings,
                         TimeStretchProcessMode mode) = 0;
    [[nodiscard]] virtual TimeStretchStatus status(const TimeStretchSettings& settings) const noexcept = 0;
};

class SignalsmithCompatibleFallbackTimeStretchEngine final : public TimeStretchEngine {
public:
    [[nodiscard]] TimeStretchEngineKind kind() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] std::uint32_t latencyFrames() const noexcept override;
    void prepare(std::uint32_t sampleRateHz,
                 std::uint32_t maxBlockFrames,
                 const TimeStretchSettings& settings,
                 TimeStretchProcessMode mode) override;
    [[nodiscard]] TimeStretchStatus status(const TimeStretchSettings& settings) const noexcept override;

private:
    std::uint32_t sampleRateHz_{44100};
    std::uint32_t maxBlockFrames_{512};
    TimeStretchProcessMode mode_{TimeStretchProcessMode::LiveRealTime};
};

#if DJAPP_HAS_RUBBERBAND
class RubberBandTimeStretchEngine final : public TimeStretchEngine {
public:
    RubberBandTimeStretchEngine();
    ~RubberBandTimeStretchEngine() override;
    RubberBandTimeStretchEngine(const RubberBandTimeStretchEngine&) = delete;
    RubberBandTimeStretchEngine& operator=(const RubberBandTimeStretchEngine&) = delete;

    [[nodiscard]] TimeStretchEngineKind kind() const noexcept override;
    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] std::uint32_t latencyFrames() const noexcept override;
    void prepare(std::uint32_t sampleRateHz,
                 std::uint32_t maxBlockFrames,
                 const TimeStretchSettings& settings,
                 TimeStretchProcessMode mode) override;
    [[nodiscard]] TimeStretchStatus status(const TimeStretchSettings& settings) const noexcept override;

private:
    struct Impl;
    Impl* impl_{};
    std::uint32_t latencyFrames_{};
    std::uint32_t preferredStartPadFrames_{};
    std::uint32_t startDelayFrames_{};
    TimeStretchProcessMode mode_{TimeStretchProcessMode::LiveRealTime};
};
#endif

[[nodiscard]] bool rubberBandTimeStretchAvailable() noexcept;
[[nodiscard]] TimeStretchEngineKind primaryTimeStretchEngineKind() noexcept;
[[nodiscard]] double playbackRateForSettings(const TimeStretchSettings& settings) noexcept;
[[nodiscard]] double effectiveTempoForSettings(const TimeStretchSettings& settings) noexcept;
[[nodiscard]] double pitchDriftCentsForSettings(const TimeStretchSettings& settings) noexcept;
[[nodiscard]] const char* toString(TimeStretchEngineKind kind) noexcept;
[[nodiscard]] const char* toString(TimeStretchProcessMode mode) noexcept;

}
