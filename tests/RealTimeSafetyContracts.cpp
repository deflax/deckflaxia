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

class FixedCommandQueue final : public djapp::audio::AudioGraphCommandQueue {
public:
    bool tryPushFromMessageThread(const djapp::audio::AudioGraphCommand& command) noexcept override {
        if (count_ == commands_.size()) {
            return false;
        }

        commands_[count_] = command;
        ++count_;
        return true;
    }

    bool tryPopForAudioThread(djapp::audio::AudioGraphCommand& command) noexcept override {
        if (readIndex_ == count_) {
            return false;
        }

        command = commands_[readIndex_];
        ++readIndex_;
        return true;
    }

private:
    std::array<djapp::audio::AudioGraphCommand, 4> commands_{};
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

static_assert(djapp::audio::areAudioCallbackDependenciesAllowed<
              const djapp::audio::ImmutableAudioSnapshot&,
              djapp::audio::AudioGraphCommandQueue&,
              djapp::audio::AudioCallbackContract>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<UiPanelService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<DatabaseService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<PluginScanService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<FilesystemImportService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<LoggerService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<BlockingMutexService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<AnalysisService>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<djapp::app::UiToEngineCommandQueue>);
static_assert(!djapp::audio::isAudioCallbackDependencyAllowed<djapp::core::CancellableBackgroundWorker>);
static_assert(std::is_trivially_copyable_v<djapp::audio::AudioGraphCommand>);
static_assert(std::is_trivially_copyable_v<djapp::audio::ImmutableAudioSnapshot>);
static_assert(djapp::audio::kAlphaSampleRateBufferMatrix.size() == 6);

int main() {
    FixedCommandQueue queue;
    djapp::app::UiToEngineCommandQueue uiCommands(queue);
    const djapp::audio::ImmutableAudioSnapshot snapshot{42, 0.75F, 4};
    const djapp::audio::AudioCallbackContract callback(snapshot, queue);

    if (expect(callback.snapshot().revision() == 42, "callback contract should expose immutable snapshot") != 0) {
        return 1;
    }

    const djapp::audio::AudioGraphCommand command{djapp::audio::AudioGraphCommandKind::SetDeckGain, 2, 0.5F, 0};
    if (expect(uiCommands.trySendFromMessageThread(command), "message thread should enqueue graph command") != 0) {
        return 1;
    }

    djapp::audio::AudioGraphCommand popped{};
    if (expect(callback.commandQueue().tryPopForAudioThread(popped), "audio thread should pop graph command") != 0) {
        return 1;
    }

    if (expect(popped.kind == djapp::audio::AudioGraphCommandKind::SetDeckGain, "command kind should round trip") != 0) {
        return 1;
    }

    if (expect(popped.targetId == 2, "command target should round trip") != 0) {
        return 1;
    }

    std::cout << "Real-time safety contract tests passed\n";
    return 0;
}
