#include "app/EngineCommandQueue.h"
#include "audio/AudioGraphContracts.h"
#include "core/BackgroundWorkerContracts.h"

#include <array>
#include <iostream>
#include <string>
#include <type_traits>

namespace {

struct UiPanelService {};
struct DatabaseService {};
struct PluginScanService {};
struct FilesystemImportService {};
struct LoggerService {};
struct BlockingMutexService {};
struct AnalysisService {};

class FixedCommandQueue final : public deckflaxia::audio::AudioGraphCommandQueue {
public:
    bool tryPushFromMessageThread(const deckflaxia::audio::AudioGraphCommand& command) noexcept override {
        if (count_ == commands_.size()) {
            return false;
        }

        commands_[count_] = command;
        ++count_;
        return true;
    }

    bool tryPopForAudioThread(deckflaxia::audio::AudioGraphCommand& command) noexcept override {
        if (readIndex_ == count_) {
            return false;
        }

        command = commands_[readIndex_];
        ++readIndex_;
        return true;
    }

private:
    std::array<deckflaxia::audio::AudioGraphCommand, 4> commands_{};
    std::size_t count_{};
    std::size_t readIndex_{};
};

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

}

static_assert(deckflaxia::audio::areAudioCallbackDependenciesAllowed<
              const deckflaxia::audio::ImmutableAudioSnapshot&,
              deckflaxia::audio::AudioGraphCommandQueue&,
              deckflaxia::audio::AudioCallbackContract>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<UiPanelService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<DatabaseService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<PluginScanService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<FilesystemImportService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<LoggerService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<BlockingMutexService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<AnalysisService>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<deckflaxia::app::UiToEngineCommandQueue>);
static_assert(!deckflaxia::audio::isAudioCallbackDependencyAllowed<deckflaxia::core::CancellableBackgroundWorker>);
static_assert(std::is_trivially_copyable_v<deckflaxia::audio::AudioGraphCommand>);
static_assert(std::is_trivially_copyable_v<deckflaxia::audio::ImmutableAudioSnapshot>);
static_assert(deckflaxia::audio::kAlphaSampleRateBufferMatrix.size() == 6);

int main() {
    FixedCommandQueue queue;
    deckflaxia::app::UiToEngineCommandQueue uiCommands(queue);
    const deckflaxia::audio::ImmutableAudioSnapshot snapshot{42, 0.75F, 4};
    const deckflaxia::audio::AudioCallbackContract callback(snapshot, queue);

    if (expect(callback.snapshot().revision() == 42, "callback contract should expose immutable snapshot") != 0) {
        return 1;
    }

    const deckflaxia::audio::AudioGraphCommand command{deckflaxia::audio::AudioGraphCommandKind::SetDeckGain, 2, 0.5F, 0};
    if (expect(uiCommands.trySendFromMessageThread(command), "message thread should enqueue graph command") != 0) {
        return 1;
    }

    deckflaxia::audio::AudioGraphCommand popped{};
    if (expect(callback.commandQueue().tryPopForAudioThread(popped), "audio thread should pop graph command") != 0) {
        return 1;
    }

    if (expect(popped.kind == deckflaxia::audio::AudioGraphCommandKind::SetDeckGain, "command kind should round trip") != 0) {
        return 1;
    }

    if (expect(popped.targetId == 2, "command target should round trip") != 0) {
        return 1;
    }

    std::cout << "Real-time safety contract tests passed\n";
    return 0;
}
