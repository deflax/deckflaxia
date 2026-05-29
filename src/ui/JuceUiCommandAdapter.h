#pragma once

#include "audio/MixerControls.h"
#include "audio/routing/AudioRoutingGraph.h"
#include "core/DomainModels.h"
#include "decks/FourDeckPlaybackCore.h"
#include "library/AudioImport.h"
#include "plugins/PluginChainProcessor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deckflaxia::ui {

enum class JuceUiCommandStatus : std::uint8_t {
    Succeeded,
    Unsupported,
    Unavailable,
    InvalidArgument,
    BackendRejected,
};

enum class JuceUiCommandDomain : std::uint8_t {
    DeckTransport,
    Mixer,
    Browser,
    PluginChain,
};

struct JuceUiCommandResult final {
    JuceUiCommandStatus status{JuceUiCommandStatus::Succeeded};
    JuceUiCommandDomain domain{JuceUiCommandDomain::DeckTransport};
    std::string action;
    std::string detail;

    [[nodiscard]] bool ok() const noexcept { return status == JuceUiCommandStatus::Succeeded; }
};

enum class DeckTransportAction : std::uint8_t {
    Play,
    Pause,
    Stop,
    Cue,
    Sync,
};

struct DeckTransportIntent final {
    DeckTransportAction action{DeckTransportAction::Play};
    std::size_t deckIndex{};
    double sourceBpm{120.0};
    double targetBpm{120.0};
    bool pitchLockEnabled{true};
};

enum class MixerAction : std::uint8_t {
    Volume,
    Gain,
    EqLow,
    EqMid,
    EqHigh,
    Crossfader,
    Cue,
    Output,
};

struct MixerIntent final {
    MixerAction action{MixerAction::Volume};
    std::size_t deckIndex{};
    float value{};
    bool enabled{};
    core::OutputBus output{core::OutputBus::Master};
};

enum class BrowserAction : std::uint8_t {
    Import,
    LoadToDeck,
    SelectRow,
};

struct BrowserIntent final {
    BrowserAction action{BrowserAction::Import};
    library::FilesystemEntry entry;
    std::size_t deckIndex{};
    std::size_t rowIndex{};
};

enum class PluginChainAction : std::uint8_t {
    Bypass,
    Remove,
    MoveUp,
    MoveDown,
    OpenEditor,
    CloseEditor,
    Parameter,
};

struct PluginChainIntent final {
    PluginChainAction action{PluginChainAction::Bypass};
    plugins::PluginChainTargetKind target{plugins::PluginChainTargetKind::Deck};
    std::size_t deckIndex{};
    std::size_t slotIndex{};
    bool bypassed{};
    std::string parameterId;
    double normalizedValue{};
};

struct JuceUiCommandAdapterServices final {
    decks::FourDeckPlaybackCore* playbackCore{};
    audio::MixerController* mixer{};
    audio::routing::AudioRoutingGraphController* routing{};
    plugins::OfflinePluginChainHost* pluginHost{};
    core::PluginChainDescriptor* pluginDescriptor{};
    std::vector<library::AudioImportClassification>* browserRows{};
};

class JuceUiCommandAdapter final {
public:
    explicit JuceUiCommandAdapter(JuceUiCommandAdapterServices services) noexcept;

    [[nodiscard]] JuceUiCommandResult dispatch(const DeckTransportIntent& intent) noexcept;
    [[nodiscard]] JuceUiCommandResult dispatch(const MixerIntent& intent) noexcept;
    [[nodiscard]] JuceUiCommandResult dispatch(const BrowserIntent& intent);
    [[nodiscard]] JuceUiCommandResult dispatch(const PluginChainIntent& intent) noexcept;

private:
    [[nodiscard]] JuceUiCommandResult dispatchToPlaybackCore(const DeckTransportIntent& intent) noexcept;
    [[nodiscard]] JuceUiCommandResult dispatchToMixer(const DeckTransportIntent& intent) noexcept;
    [[nodiscard]] JuceUiCommandResult dispatchDescriptorPluginIntent(const PluginChainIntent& intent) noexcept;
    [[nodiscard]] JuceUiCommandResult dispatchHostPluginIntent(const PluginChainIntent& intent) noexcept;

    JuceUiCommandAdapterServices services_;
};

[[nodiscard]] const char* toString(JuceUiCommandStatus status) noexcept;
[[nodiscard]] const char* toString(JuceUiCommandDomain domain) noexcept;
[[nodiscard]] const char* toString(DeckTransportAction action) noexcept;
[[nodiscard]] const char* toString(MixerAction action) noexcept;
[[nodiscard]] const char* toString(BrowserAction action) noexcept;
[[nodiscard]] const char* toString(PluginChainAction action) noexcept;

}
