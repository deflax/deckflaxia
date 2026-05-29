#pragma once

#include "app/UiShell.h"
#include "ui/JuceUiCommandAdapter.h"

#include <array>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <vector>

#if DECKFLAXIA_HAS_JUCE
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#endif

namespace deckflaxia::ui {

#if DECKFLAXIA_HAS_JUCE

class MainComponent final : public juce::Component {
public:
    explicit MainComponent(bool noAudioDevice);
    ~MainComponent() override;

    [[nodiscard]] bool audioDeviceManagerInitialized() const noexcept;
    [[nodiscard]] bool commandManagerPresent() const noexcept;
    [[nodiscard]] const juce::String& audioInitializationMessage() const noexcept;
    [[nodiscard]] const app::HybridUiShellSnapshot& snapshot() const noexcept;
    [[nodiscard]] const decks::FourDeckPlaybackCore& playbackCore() const noexcept;
    [[nodiscard]] const audio::MixerSnapshot& mixerSnapshot() const noexcept;
    [[nodiscard]] const std::vector<library::AudioImportClassification>& browserRows() const noexcept;

    void refreshFromAuthoritativeState();

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class DeckComponent;
    class MixerComponent;
    class BrowserComponent;
    class WaveformComponent;
    class BeatgridEditorComponent;
    class PluginChainComponent;
    class MidiLearnComponent;
    class StatusBarComponent;
    class AudioSettingsComponent;

    juce::AudioDeviceManager audioDeviceManager_;
    juce::ApplicationCommandManager commandManager_;
    juce::String audioInitializationMessage_;
    bool audioDeviceManagerInitialized_{};
    app::HybridUiShellModel shellModel_;
    app::HybridUiShellSnapshot snapshot_;
    decks::FourDeckPlaybackCore playbackCore_;
    std::vector<library::AudioImportClassification> browserRows_;
    JuceUiCommandAdapter commandAdapter_;
    std::array<std::unique_ptr<DeckComponent>, audio::routing::kDeckCount> decks_{};
    std::unique_ptr<MixerComponent> mixer_;
    std::unique_ptr<BrowserComponent> browser_;
    std::unique_ptr<WaveformComponent> waveform_;
    std::unique_ptr<BeatgridEditorComponent> beatgridEditor_;
    std::unique_ptr<PluginChainComponent> pluginChain_;
    std::unique_ptr<MidiLearnComponent> midiLearn_;
    std::unique_ptr<StatusBarComponent> statusBar_;
    std::unique_ptr<AudioSettingsComponent> audioSettings_;
};

void writeComponentTreeReport(const MainComponent& component, std::ostream& output);
void writeHeadlessComponentTreeReport(std::ostream& output);
[[nodiscard]] bool writeComponentScreenshot(MainComponent& component, const std::filesystem::path& screenshotPath, std::ostream& output);
[[nodiscard]] bool writeHeadlessComponentScreenshot(const std::filesystem::path& screenshotPath, std::ostream& output);

#endif

[[nodiscard]] int runUnavailableJuceUiSmoke(std::ostream& output,
                                           bool dumpComponents,
                                           const std::filesystem::path& screenshotPath);

}
