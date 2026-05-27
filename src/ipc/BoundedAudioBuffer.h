#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace deckflaxia::ipc {

class BoundedAudioBuffer final {
public:
    BoundedAudioBuffer() = default;
    BoundedAudioBuffer(std::uint32_t channels, std::uint32_t maxFrames) { configure(channels, maxFrames); }

    void configure(std::uint32_t channels, std::uint32_t maxFrames) {
        channels_ = channels;
        maxFrames_ = maxFrames;
        input_.assign(static_cast<std::size_t>(channels_) * maxFrames_, 0.0F);
        output_.assign(static_cast<std::size_t>(channels_) * maxFrames_, 0.0F);
    }

    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t maxFrames() const noexcept { return maxFrames_; }
    [[nodiscard]] std::uint32_t frameCount() const noexcept { return frameCount_; }
    [[nodiscard]] const float* inputData() const noexcept { return input_.data(); }
    [[nodiscard]] float* outputData() noexcept { return output_.data(); }
    [[nodiscard]] const float* outputData() const noexcept { return output_.data(); }

    [[nodiscard]] bool writeInput(const float* interleaved, std::uint32_t frames) noexcept {
        if (interleaved == nullptr || channels_ == 0U || frames > maxFrames_) {
            return false;
        }
        frameCount_ = frames;
        const auto samples = static_cast<std::size_t>(frames) * channels_;
        std::copy_n(interleaved, samples, input_.begin());
        std::copy_n(interleaved, samples, output_.begin());
        return true;
    }

    [[nodiscard]] bool readOutput(float* interleaved, std::uint32_t frames) const noexcept {
        if (interleaved == nullptr || frames > frameCount_) {
            return false;
        }
        std::copy_n(output_.begin(), static_cast<std::size_t>(frames) * channels_, interleaved);
        return true;
    }

private:
    std::uint32_t channels_{};
    std::uint32_t maxFrames_{};
    std::uint32_t frameCount_{};
    std::vector<float> input_{};
    std::vector<float> output_{};
};

}
