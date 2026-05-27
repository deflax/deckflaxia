#pragma once

#include "audio/AudioGraphContracts.h"

namespace deckflaxia::app {

enum class EngineCommandSource {
    MessageThread,
    BackgroundWorker,
};

class UiToEngineCommandQueue final {
public:
    explicit constexpr UiToEngineCommandQueue(audio::AudioGraphCommandQueue& commands) noexcept
        : commands_(&commands) {}

    [[nodiscard]] bool trySendFromMessageThread(const audio::AudioGraphCommand& command) noexcept {
        return commands_->tryPushFromMessageThread(command);
    }

private:
    audio::AudioGraphCommandQueue* commands_{};
};

}
