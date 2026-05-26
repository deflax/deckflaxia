#pragma once

#include <cstdint>

namespace djapp::core {

enum class BackgroundWorkerRole : std::uint8_t {
    AnalysisPool,
    PluginScanWorker,
    DatabaseWorker,
    WaveformWorker,
};

enum class BackgroundJobKind : std::uint8_t {
    AnalyzeTrack,
    ScanPlugins,
    PersistLibraryChange,
    BuildWaveformSummary,
};

struct BackgroundJobTicket final {
    std::uint64_t id{};
    BackgroundJobKind kind{};
    BackgroundWorkerRole role{};
};

class CancellableBackgroundWorker {
public:
    CancellableBackgroundWorker() = default;
    CancellableBackgroundWorker(const CancellableBackgroundWorker&) = delete;
    CancellableBackgroundWorker& operator=(const CancellableBackgroundWorker&) = delete;
    virtual ~CancellableBackgroundWorker() = default;

    virtual bool trySchedule(BackgroundJobTicket ticket) noexcept = 0;
    virtual void requestStop() noexcept = 0;
    [[nodiscard]] virtual bool stopRequested() const noexcept = 0;
};

}
