#pragma once

#include "audio/routing/AudioRoutingGraph.h"
#include "audio/MixerControls.h"
#include "library/LibraryModel.h"
#include "midi/MidiLearn.h"
#include "rendering/WaveformRenderer.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace djapp::app {

struct BrowserTrackViewModel final {
    std::string trackId;
    std::string title;
    std::string artist;
    bool missing{};
    bool waveformAvailable{};
};

struct BrowserPanelViewModel final {
    std::string componentName{"browser-library-panel"};
    std::vector<BrowserTrackViewModel> tracks;
    bool empty{};
    std::string statusText;
};

struct DeckPanelViewModel final {
    std::string componentName;
    std::string displayName;
    std::string accentName;
    rendering::RendererFrame waveform;
    rendering::RendererFrame meter;
};

struct RoutingPanelViewModel final {
    std::string componentName{"routing-panel"};
    std::size_t nodeCount{};
    std::size_t connectionCount{};
    std::string statusText;
};

struct MixerDeckControlViewModel final {
    std::string componentName;
    float volume{1.0F};
    float gain{1.0F};
    float eqLow{1.0F};
    float eqMid{1.0F};
    float eqHigh{1.0F};
    bool cueEnabled{};
    bool playing{};
    float meterLeft{};
    float meterRight{};
};

struct MixerPanelViewModel final {
    std::string componentName{"mixer-controls-panel"};
    std::array<MixerDeckControlViewModel, audio::routing::kDeckCount> decks{};
    float crossfader{0.5F};
    std::string statusText;
};

struct PluginParameterDisplayViewModel final {
    std::string componentName;
    std::string displayName;
    double normalizedValue{};
};

struct PluginSlotViewModel final {
    std::string componentName;
    std::string displayName;
    std::string statusText;
    std::string boundaryStatus;
    bool placeholder{true};
    bool bypassed{};
    bool removable{};
    bool canMoveUp{};
    bool canMoveDown{};
    bool nativeEditorAvailable{};
    std::vector<PluginParameterDisplayViewModel> parameters;
};

struct PluginChainPanelViewModel final {
    std::string componentName{"plugin-chain-panel"};
    std::vector<PluginSlotViewModel> slots;
};

struct MidiLearnIndicatorViewModel final {
    std::string componentName{"midi-learn-indicator"};
    bool learning{};
    std::size_t mappingCount{};
    std::string statusText;
};

struct AppStatusViewModel final {
    std::string componentName{"app-status-errors"};
    bool ok{true};
    std::vector<std::string> errors;
    std::string statusText;
};

struct HybridUiShellSnapshot final {
    std::array<DeckPanelViewModel, audio::routing::kDeckCount> decks{};
    BrowserPanelViewModel browser;
    MixerPanelViewModel mixer;
    RoutingPanelViewModel routing;
    PluginChainPanelViewModel pluginChain;
    MidiLearnIndicatorViewModel midiLearn;
    AppStatusViewModel status;
};

struct MidiLearnIndicatorSnapshot final {
    bool learning{};
    std::size_t mappingCount{};
    std::string learningTargetName;
};

struct HybridUiShellInputSnapshot final {
    std::vector<library::BrowserTrackEntry> browserTracks;
    audio::routing::AudioRoutingGraphSnapshot routing;
    audio::MixerSnapshot mixer;
    MidiLearnIndicatorSnapshot midiLearn;
};

class HybridUiShellModel final {
public:
    explicit HybridUiShellModel(rendering::WaveformRenderer renderer = rendering::WaveformRenderer{});

    [[nodiscard]] HybridUiShellSnapshot buildSnapshot(const HybridUiShellInputSnapshot& input) const;
    [[nodiscard]] std::string formatSmokeReport(const HybridUiShellSnapshot& snapshot) const;

private:
    rendering::WaveformRenderer renderer_;
};

[[nodiscard]] HybridUiShellInputSnapshot createUiSmokeInput(bool emptyLibrary);
[[nodiscard]] int runUiSmokeTest(bool emptyLibrary);

}
