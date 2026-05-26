#include "rendering/WaveformRenderer.h"

#include <algorithm>

namespace djapp::rendering {

WaveformRenderer::WaveformRenderer(RendererBackend backend) noexcept : backend_(backend) {}

RendererBackend WaveformRenderer::backend() const noexcept {
    return backend_;
}

RendererFrame WaveformRenderer::renderWaveform(const WaveformRenderRequest& request) const {
    RendererFrame frame;
    frame.componentName = "waveform-renderer";
    frame.backend = backend_;
    frame.placeholder = request.summaryPoints.empty();
    frame.primitiveCount = frame.placeholder ? 1U : request.summaryPoints.size();
    frame.statusText = frame.placeholder ? "waveform-placeholder: no analyzed waveform" : "waveform-ready: summary-points=" + std::to_string(request.summaryPoints.size());
    return frame;
}

RendererFrame WaveformRenderer::renderMeter(const MeterRenderRequest& request) const {
    const auto left = std::max(0.0F, request.peakLeft);
    const auto right = std::max(0.0F, request.peakRight);
    RendererFrame frame;
    frame.componentName = "meter-renderer";
    frame.backend = backend_;
    frame.placeholder = left == 0.0F && right == 0.0F;
    frame.primitiveCount = 2;
    frame.statusText = frame.placeholder ? "meter-idle: silence" : "meter-active: stereo-peak";
    return frame;
}

const char* toString(RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::CpuVectorPlaceholder:
        return "cpu-vector-placeholder";
    case RendererBackend::AcceleratedCustom:
        return "accelerated-custom";
    }
    return "unknown";
}

}
