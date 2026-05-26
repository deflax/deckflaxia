#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace djapp::rendering {

enum class RendererBackend : std::uint8_t {
    CpuVectorPlaceholder,
    AcceleratedCustom,
};

struct WaveformRenderRequest final {
    std::string trackId;
    std::vector<float> summaryPoints;
    std::uint32_t width{640};
    std::uint32_t height{160};
};

struct MeterRenderRequest final {
    float peakLeft{};
    float peakRight{};
    std::uint32_t height{120};
};

struct RendererFrame final {
    std::string componentName;
    RendererBackend backend{RendererBackend::CpuVectorPlaceholder};
    bool placeholder{true};
    std::size_t primitiveCount{};
    std::string statusText;
};

class WaveformRenderer final {
public:
    explicit WaveformRenderer(RendererBackend backend = RendererBackend::CpuVectorPlaceholder) noexcept;

    [[nodiscard]] RendererBackend backend() const noexcept;
    [[nodiscard]] RendererFrame renderWaveform(const WaveformRenderRequest& request) const;
    [[nodiscard]] RendererFrame renderMeter(const MeterRenderRequest& request) const;

private:
    RendererBackend backend_;
};

[[nodiscard]] const char* toString(RendererBackend backend) noexcept;

}
