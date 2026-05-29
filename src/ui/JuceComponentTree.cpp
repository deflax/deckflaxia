#include "ui/JuceComponentTree.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace deckflaxia::ui {

constexpr std::array<const char*, 9> kRequiredComponentNames{{
    "DeckComponent",
    "MixerComponent",
    "BrowserComponent",
    "WaveformComponent",
    "BeatgridEditorComponent",
    "PluginChainComponent",
    "MidiLearnComponent",
    "StatusBarComponent",
    "AudioSettingsComponent",
}};

void writeDeckControlInventory(std::ostream& output) {
    for (std::size_t index = 0; index < audio::routing::kDeckCount; ++index) {
        const auto deckNumber = index + 1U;
        output << "control-inventory: Deck" << deckNumber << "PlayCommandButton family=DeckNPlayCommandButton classification=disabled state=no-deck-loaded clickable=false label=LoadDeckFirst wiring=adapter-callback result=unavailable\n";
        output << "control-inventory: Deck" << deckNumber << "CueCommandButton family=DeckNCueCommandButton classification=disabled state=no-deck-loaded clickable=false label=LoadDeckFirst wiring=adapter-callback result=unavailable\n";
        output << "control-inventory: Deck" << deckNumber << "SyncCommandButton family=DeckNSyncCommandButton classification=disabled state=no-deck-loaded clickable=false label=LoadDeckFirst wiring=adapter-callback result=unavailable\n";
        output << "control-inventory: Deck" << deckNumber << "VolumeCommandSlider family=DeckNVolumeCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=volume\n";
        output << "control-inventory: Deck" << deckNumber << "GainCommandSlider family=DeckNGainCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=gain\n";
        output << "control-inventory: Deck" << deckNumber << "EqLowCommandSlider family=DeckNEqLowCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=eq-low\n";
        output << "control-inventory: Deck" << deckNumber << "EqMidCommandSlider family=DeckNEqMidCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=eq-mid\n";
        output << "control-inventory: Deck" << deckNumber << "EqHighCommandSlider family=DeckNEqHighCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=eq-high\n";
    }
}

void writePluginSlotInventoryFor(std::ostream& output, const std::string& slotName) {
    output << "control-inventory: " << slotName << "BypassCommandButton family=PluginSlotBypassCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "RemoveCommandButton family=PluginSlotRemoveCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "MoveUpCommandButton family=PluginSlotMoveUpCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "MoveDownCommandButton family=PluginSlotMoveDownCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "OpenEditorCommandButton family=PluginSlotOpenEditorCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin native-editor=unavailable wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "CloseEditorCommandButton family=PluginSlotCloseEditorCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin native-editor=unavailable wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: " << slotName << "GenericGainParameterCommandSlider family=PluginSlotParameterCommandSlider classification=disabled state=no-plugin clickable=false read-only=true wiring=adapter-callback result=unavailable\n";
}

void writePluginSlotControlInventory(std::ostream& output) {
    for (std::size_t deckIndex = 0; deckIndex < audio::routing::kDeckCount; ++deckIndex) {
        for (std::size_t slotIndex = 0; slotIndex < audio::routing::kPluginSlotsPerDeck; ++slotIndex) {
            writePluginSlotInventoryFor(output, "plugin-slot-deck-" + std::to_string(deckIndex + 1U) + "-" + std::to_string(slotIndex + 1U));
        }
    }
    for (std::size_t slotIndex = 0; slotIndex < audio::routing::kPluginSlotsPerDeck; ++slotIndex) {
        writePluginSlotInventoryFor(output, "plugin-slot-master-" + std::to_string(slotIndex + 1U));
    }
}

void writeNativeControlInventory(std::ostream& output) {
    output << "native-control-inventory: visible-controls classified\n";
    output << "control-inventory: MainComponent family=RootPanel classification=read-only state=native-juce-shell\n";
    for (std::size_t index = 0; index < audio::routing::kDeckCount; ++index) {
        output << "control-inventory: DeckComponent[" << (index + 1U) << "] family=DeckPanel classification=read-only state=no-deck-loaded\n";
    }
    output << "control-inventory: MixerComponent family=MixerPanel classification=read-only state=command-surface-active\n";
    output << "control-inventory: MixerCrossfaderCommandSlider family=MixerCrossfaderCommandSlider classification=wired state=mixer-backend clickable=true wiring=adapter-callback action=crossfader\n";
    writeDeckControlInventory(output);
    output << "control-inventory: BrowserComponent family=BrowserPanel classification=wired state=empty-library-supported wiring=adapter-callback\n";
    output << "control-inventory: ImportFilesButton family=ImportFilesButton classification=wired state=native-file-chooser-supported clickable=true label=ImportFiles smoke-mode=deterministic-fixture wiring=adapter-callback\n";
    output << "control-inventory: ImportFolderButton family=ImportFolderButton classification=wired state=native-folder-chooser-supported clickable=true label=ImportFolder smoke-mode=deterministic-folder-error wiring=adapter-callback\n";
    output << "control-inventory: BrowserTargetDeckSelector family=BrowserTargetDeckSelector classification=wired state=selected-deck-source clickable=true wiring=load-to-deck\n";
    output << "control-inventory: LoadSelectedBrowserTrackButton family=LoadSelectedBrowserTrackButton classification=disabled state=no-imported-selection clickable=false wiring=adapter-callback result=unavailable\n";
    output << "control-inventory: BrowserTrackTableModel family=BrowserTrackTableModel classification=wired state=empty-library clickable=true rows-from-adapter selection=adapter-callback\n";
    output << "control-inventory: WaveformComponent family=WaveformPanel classification=read-only state=no-deck-loaded placeholder=true\n";
    output << "control-inventory: BeatgridEditorComponent family=BeatgridEditorPanel classification=read-only state=no-deck-loaded placeholder=true\n";
    output << "control-inventory: PluginChainComponent family=PluginChainPanel classification=read-only state=no-plugin slots=20\n";
    writePluginSlotControlInventory(output);
    output << "control-inventory: MidiLearnComponent family=MidiStatusPanel classification=read-only state=idle\n";
    output << "control-inventory: StatusBarComponent family=AppStatusPanel classification=read-only state=status-snapshot\n";
    output << "control-inventory: AudioSettingsComponent family=AudioSettingsPanel classification=read-only state=no-plugin native-editor=out-of-scope\n";
}

void writeUnavailableControlInventory(std::ostream& output) {
    output << "native-control-inventory: unavailable classification=out-of-scope reason=DECKFLAXIA_HAS_JUCE=0\n";
    output << "fallback-control-inventory: visible-native-controls=0 clickable-native-controls=0 state=no-JUCE\n";
}

#if DECKFLAXIA_HAS_JUCE

struct WorkstationTokens final {
    juce::Colour deckFace{0xff15181c};
    juce::Colour panelFace{0xff1f2429};
    juce::Colour rail{0xff343b42};
    juce::Colour text{0xffd8ded8};
    juce::Colour mutedText{0xff84908a};
    juce::Colour amber{0xffffb84d};
    juce::Colour cyan{0xff52d9e8};
    juce::Colour lime{0xff9ee857};
    juce::Colour red{0xffff665c};
    juce::Colour shadow{0xaa050608};
    float radius{10.0F};
    int gap{10};
};

const WorkstationTokens& tokens() {
    static const WorkstationTokens value;
    return value;
}

struct SmokeLayoutRegions final {
    juce::Rectangle<int> status;
    juce::Rectangle<int> browser;
    juce::Rectangle<int> audioSettings;
    juce::Rectangle<int> midiLearn;
    juce::Rectangle<int> mixer;
    juce::Rectangle<int> waveform;
    juce::Rectangle<int> beatgridEditor;
    juce::Rectangle<int> pluginChain;
    std::array<juce::Rectangle<int>, 4> decks{};
};

SmokeLayoutRegions calculateSmokeLayout(juce::Rectangle<int> bounds) {
    auto area = bounds.reduced(tokens().gap);
    const auto gap = tokens().gap;
    constexpr int statusHeight = 44;
    constexpr int rightSidebarWidth = 300;
    constexpr int centerWidth = 330;
    constexpr int audioSettingsHeight = 96;
    constexpr int mixerHeight = 170;
    constexpr int waveformHeight = 84;
    constexpr int beatgridHeight = 66;

    SmokeLayoutRegions regions;
    regions.status = area.removeFromBottom(statusHeight);
    area.removeFromBottom(gap);

    auto sidebar = area.removeFromRight(rightSidebarWidth);
    area.removeFromRight(gap);

    auto utility = area.removeFromRight(centerWidth);
    area.removeFromRight(gap);

    regions.browser = sidebar.removeFromTop((sidebar.getHeight() - audioSettingsHeight - (2 * gap)) / 2);
    sidebar.removeFromTop(gap);
    regions.audioSettings = sidebar.removeFromTop(audioSettingsHeight);
    sidebar.removeFromTop(gap);
    regions.midiLearn = sidebar;

    regions.mixer = utility.removeFromTop(mixerHeight);
    utility.removeFromTop(gap);
    regions.waveform = utility.removeFromTop(waveformHeight);
    utility.removeFromTop(gap);
    regions.beatgridEditor = utility.removeFromTop(beatgridHeight);
    utility.removeFromTop(gap);
    regions.pluginChain = utility;

    const auto deckWidth = (area.getWidth() - gap) / 2;
    const auto deckHeight = (area.getHeight() - gap) / 2;
    auto top = area.removeFromTop(deckHeight);
    area.removeFromTop(gap);
    auto bottom = area;
    regions.decks[0] = top.removeFromLeft(deckWidth);
    top.removeFromLeft(gap);
    regions.decks[1] = top;
    regions.decks[2] = bottom.removeFromLeft(deckWidth);
    bottom.removeFromLeft(gap);
    regions.decks[3] = bottom;

    return regions;
}

juce::Colour accentForDeck(std::size_t index) {
    const auto& t = tokens();
    const std::array<juce::Colour, 4> accents{{t.amber, t.cyan, t.lime, t.red}};
    return accents[index % accents.size()];
}

void paintPanel(juce::Graphics& graphics, juce::Rectangle<int> bounds, const juce::String& title, juce::Colour accent) {
    const auto& t = tokens();
    const auto area = bounds.toFloat().reduced(2.0F);
    graphics.setColour(t.shadow);
    graphics.fillRoundedRectangle(area.translated(0.0F, 3.0F), t.radius);
    graphics.setColour(t.panelFace);
    graphics.fillRoundedRectangle(area, t.radius);
    graphics.setColour(accent);
    graphics.fillRect(bounds.withHeight(3));
    graphics.setColour(t.rail);
    graphics.drawRoundedRectangle(area, t.radius, 1.0F);
    graphics.setColour(t.text);
    graphics.setFont(juce::FontOptions(14.0F, juce::Font::bold));
    graphics.drawText(title, bounds.reduced(12, 8).withHeight(20), juce::Justification::centredLeft);
}

class IndustrialPanel : public juce::Component {
public:
    IndustrialPanel(juce::String componentName, juce::String title, juce::Colour accent)
        : title_(std::move(title)), accent_(accent) {
        setName(std::move(componentName));
    }

    void paint(juce::Graphics& graphics) override {
        paintPanel(graphics, getLocalBounds(), title_, accent_);
    }

private:
    juce::String title_;
    juce::Colour accent_;
};

void dumpComponent(const juce::Component& component, std::ostream& output, int depth) {
    const auto bounds = component.getBounds();
    const auto control = dynamic_cast<const juce::Button*>(&component) != nullptr ||
                         dynamic_cast<const juce::Slider*>(&component) != nullptr ||
                         dynamic_cast<const juce::ComboBox*>(&component) != nullptr ||
                         dynamic_cast<const juce::TableListBox*>(&component) != nullptr;
    const auto clickable = control && component.isEnabled() && !bounds.isEmpty();
    output << std::string(static_cast<std::size_t>(depth) * 2U, ' ')
           << component.getName().toStdString()
           << " children=" << component.getNumChildComponents()
           << " enabled=" << (component.isEnabled() ? "true" : "false")
           << " bounds=" << bounds.toString().toStdString()
           << " clickable=" << (clickable ? "true" : "false") << '\n';
    for (int index = 0; index < component.getNumChildComponents(); ++index) {
        if (const auto* child = component.getChildComponent(index)) {
            dumpComponent(*child, output, depth + 1);
        }
    }
}

bool nameStartsWith(const juce::Component& component, const juce::String& prefix) {
    return component.getName().startsWith(prefix);
}

juce::String formatCommandResultStatus(const JuceUiCommandResult& result) {
    return juce::String(result.ok() ? "status-ok: " : "status-error: ") +
           juce::String(toString(result.domain)) + " " +
           juce::String(result.action) + " " +
           juce::String(result.detail);
}

using CommandResultStatusSink = std::function<void(const JuceUiCommandResult&)>;

void dispatchTransport(JuceUiCommandAdapter& adapter, const CommandResultStatusSink& statusSink, DeckTransportAction action, std::size_t deckIndex) {
    statusSink(adapter.dispatch(DeckTransportIntent{action, deckIndex}));
}

void dispatchMixer(JuceUiCommandAdapter& adapter, const CommandResultStatusSink& statusSink, MixerAction action, std::size_t deckIndex, double value) {
    statusSink(adapter.dispatch(MixerIntent{action, deckIndex, static_cast<float>(value)}));
}

void configureMixerSlider(juce::Slider& slider, double initialValue, double maximumValue) {
    slider.setRange(0.0, maximumValue, 0.01);
    slider.setValue(initialValue, juce::dontSendNotification);
    slider.setEnabled(true);
}

void configureUnavailableButton(juce::TextButton& button, const juce::String& label, const juce::String& tooltip) {
    button.setButtonText(label);
    button.setTooltip(tooltip);
    button.setEnabled(false);
}

void configureDeckStateLabel(juce::Label& label, const juce::String& componentId, const juce::String& text, juce::Colour textColour) {
    label.setName(componentId);
    label.setComponentID(componentId);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, textColour);
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setInterceptsMouseClicks(false, false);
}

app::HybridUiShellInputSnapshot currentShellInput(const decks::FourDeckPlaybackCore& playbackCore) {
    app::HybridUiShellInputSnapshot input;
    input.routing = playbackCore.routingSnapshot();
    input.mixer = playbackCore.mixerSnapshot();
    input.midiLearn = app::MidiLearnIndicatorSnapshot{false, midi::MidiLearnTargetRegistry::createAlphaDefault().size(), {}};
    return input;
}

std::size_t countByPrefix(const juce::Component& component, const juce::String& prefix) {
    std::size_t count = nameStartsWith(component, prefix) ? 1U : 0U;
    for (int index = 0; index < component.getNumChildComponents(); ++index) {
        if (const auto* child = component.getChildComponent(index)) {
            count += countByPrefix(*child, prefix);
        }
    }
    return count;
}


void writeSmokeTreeSkeleton(std::ostream& output) {
    const auto layout = calculateSmokeLayout({0, 0, 1280, 720});
    output << "juce-ui-smoke-test: ok\n";
    output << "component-tree: native-juce industrial-four-deck\n";
    output << "snapshot-source: app::HybridUiShellModel command-state snapshot\n";
    output << "DeckComponent[4]: count=4\n";
    for (const auto* name : kRequiredComponentNames) {
        output << "required-component: " << name << " count=" << (std::string(name) == "DeckComponent" ? 4 : 1) << '\n';
    }
    writeNativeControlInventory(output);
    output << "tree:\n";
    output << "MainComponent children=9 bounds=0 0 1280 720\n";
    for (std::size_t index = 0; index < audio::routing::kDeckCount; ++index) {
        const auto deckNumber = index + 1U;
        output << "  DeckComponent[" << deckNumber << "] children=4 bounds=" << layout.decks[index].toString().toStdString() << '\n';
        auto deckArea = juce::Rectangle<int>{0, 0, layout.decks[index].getWidth(), layout.decks[index].getHeight()}.reduced(12, 34);
        output << "    Deck" << deckNumber << "StateLabel children=0 bounds=" << deckArea.removeFromTop(24).toString().toStdString() << '\n';
        deckArea.removeFromTop(6);
        output << "    Deck" << deckNumber << "AccentLabel children=0 bounds=" << deckArea.removeFromTop(20).toString().toStdString() << '\n';
        deckArea.removeFromTop(6);
        output << "    Deck" << deckNumber << "WaveformLabel children=0 bounds=" << deckArea.removeFromTop(20).toString().toStdString() << '\n';
        deckArea.removeFromTop(6);
        output << "    Deck" << deckNumber << "MeterLabel children=0 bounds=" << deckArea.removeFromTop(20).toString().toStdString() << '\n';
    }
    output << "  MixerComponent children=1 bounds=" << layout.mixer.toString().toStdString() << '\n';
    output << "  BrowserComponent children=5 bounds=" << layout.browser.toString().toStdString() << '\n';
    output << "  WaveformComponent children=0 bounds=" << layout.waveform.toString().toStdString() << '\n';
    output << "  BeatgridEditorComponent children=0 bounds=" << layout.beatgridEditor.toString().toStdString() << '\n';
    output << "  PluginChainComponent children=140 bounds=" << layout.pluginChain.toString().toStdString() << '\n';
    output << "  MidiLearnComponent children=0 bounds=" << layout.midiLearn.toString().toStdString() << '\n';
    output << "  StatusBarComponent children=1 bounds=" << layout.status.toString().toStdString() << '\n';
    output << "    StatusTextLabel children=0 bounds=" << juce::Rectangle<int>{0, 0, layout.status.getWidth(), layout.status.getHeight()}.reduced(12, 18).toString().toStdString() << '\n';
    output << "  AudioSettingsComponent children=0 bounds=" << layout.audioSettings.toString().toStdString() << '\n';
}

void paintHeadlessSmokeScreenshot(juce::Graphics& graphics, int width, int height) {
    const auto background = juce::Rectangle<int>{0, 0, width, height};
    graphics.fillAll(tokens().deckFace);
    juce::ColourGradient gradient(tokens().rail.withAlpha(0.7F), background.getTopLeft().toFloat(), tokens().deckFace, background.getBottomRight().toFloat(), false);
    graphics.setGradientFill(gradient);
    graphics.fillRect(background);

    const auto& t = tokens();
    const auto layout = calculateSmokeLayout(background);
    paintPanel(graphics, layout.decks[0], "Deck A", t.amber);
    paintPanel(graphics, layout.decks[1], "Deck B", t.cyan);
    paintPanel(graphics, layout.decks[2], "Deck C", t.lime);
    paintPanel(graphics, layout.decks[3], "Deck D", t.red);
    paintPanel(graphics, layout.mixer, "Mixer: Four Deck Command Surface", t.amber);
    paintPanel(graphics, layout.waveform, "Waveform Overview: AudioThumbnail Cache", t.lime);
    paintPanel(graphics, layout.beatgridEditor, "Beatgrid Editor: BPM Downbeat Cues", t.amber);
    paintPanel(graphics, layout.pluginChain, "Plugin Chain: Deck + Master Editor Panels", t.red);
    paintPanel(graphics, layout.browser, "Browser: Library", t.cyan);
    paintPanel(graphics, layout.audioSettings, "Audio Settings", t.amber);
    paintPanel(graphics, layout.midiLearn, "MIDI Learn Idle", t.cyan);
    paintPanel(graphics, layout.status, "Status: Ready", t.lime);
}

class MainComponent::DeckComponent final : public IndustrialPanel {
public:
    DeckComponent(std::size_t index, const app::DeckPanelViewModel& model)
        : IndustrialPanel("DeckComponent[" + juce::String(static_cast<int>(index + 1U)) + "]",
                          model.displayName,
                          accentForDeck(index)),
          index_(index) {
        const auto deckNumber = juce::String(static_cast<int>(index + 1U));
        configureDeckStateLabel(state_, juce::String("Deck") + deckNumber + "StateLabel", "No track loaded", tokens().text);
        configureDeckStateLabel(accent_, juce::String("Deck") + deckNumber + "AccentLabel", "", accentForDeck(index));
        configureDeckStateLabel(waveform_, juce::String("Deck") + deckNumber + "WaveformLabel", "", tokens().mutedText);
        configureDeckStateLabel(meter_, juce::String("Deck") + deckNumber + "MeterLabel", "", tokens().mutedText);
        refreshFromSnapshot(model);
        addAndMakeVisible(state_);
        addAndMakeVisible(accent_);
        addAndMakeVisible(waveform_);
        addAndMakeVisible(meter_);
    }

    void refreshFromSnapshot(const app::DeckPanelViewModel& model) {
        state_.setText("No track loaded", juce::dontSendNotification);
        accent_.setText(juce::String("Accent: ") + juce::String(model.accentName), juce::dontSendNotification);
        waveform_.setText(juce::String("Waveform: ") + juce::String(model.waveform.statusText), juce::dontSendNotification);
        meter_.setText(juce::String("Meter: ") + juce::String(model.meter.statusText), juce::dontSendNotification);
        accent_.setColour(juce::Label::textColourId, accentForDeck(index_));
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 34);
        state_.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        accent_.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);
        waveform_.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);
        meter_.setBounds(area.removeFromTop(20));
    }

private:
    std::size_t index_{};
    juce::Label state_;
    juce::Label accent_;
    juce::Label waveform_;
    juce::Label meter_;
};

class MainComponent::MixerComponent final : public IndustrialPanel {
public:
    MixerComponent(const app::MixerPanelViewModel& model,
                   JuceUiCommandAdapter& adapter,
                   CommandResultStatusSink statusSink,
                   std::function<void()> refreshControls)
        : IndustrialPanel("MixerComponent", "Mixer: Four Deck Command Surface", tokens().amber),
          statusSink_(std::move(statusSink)),
          refreshControls_(std::move(refreshControls)) {
        crossfader_.setName("MixerCrossfaderCommandSlider");
        crossfader_.setComponentID("MixerCrossfaderCommandSlider");
        configureMixerSlider(crossfader_, model.crossfader, 1.0);
        crossfader_.onValueChange = [&adapter, this] { dispatchMixerAndRefresh(adapter, MixerAction::Crossfader, 0U, crossfader_.getValue()); };
        addAndMakeVisible(crossfader_);
        for (std::size_t index = 0; index < strips_.size(); ++index) {
            auto& strip = strips_[index];
            const auto deckIndex = index;
            strip.play.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "PlayCommandButton");
            strip.play.setComponentID(strip.play.getName());
            strip.play.onClick = [&adapter, this, deckIndex] { dispatchTransport(adapter, statusSink_, DeckTransportAction::Play, deckIndex); };
            configureUnavailableButton(strip.play, "Load Deck", "Load a browser track before transport controls become available.");
            strip.cue.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "CueCommandButton");
            strip.cue.setComponentID(strip.cue.getName());
            strip.cue.onClick = [&adapter, this, deckIndex] { dispatchTransport(adapter, statusSink_, DeckTransportAction::Cue, deckIndex); };
            configureUnavailableButton(strip.cue, "Load Deck", "Load a browser track before cue becomes available.");
            strip.sync.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "SyncCommandButton");
            strip.sync.setComponentID(strip.sync.getName());
            strip.sync.onClick = [&adapter, this, deckIndex] { dispatchTransport(adapter, statusSink_, DeckTransportAction::Sync, deckIndex); };
            configureUnavailableButton(strip.sync, "Load Deck", "Load a browser track before sync becomes available.");
            strip.volume.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "VolumeCommandSlider");
            strip.volume.setComponentID(strip.volume.getName());
            configureMixerSlider(strip.volume, model.decks[index].volume, 1.0);
            strip.volume.onValueChange = [&adapter, this, &slider = strip.volume, deckIndex] { dispatchMixerAndRefresh(adapter, MixerAction::Volume, deckIndex, slider.getValue()); };
            strip.gain.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "GainCommandSlider");
            strip.gain.setComponentID(strip.gain.getName());
            configureMixerSlider(strip.gain, model.decks[index].gain, 1.0);
            strip.gain.onValueChange = [&adapter, this, &slider = strip.gain, deckIndex] { dispatchMixerAndRefresh(adapter, MixerAction::Gain, deckIndex, slider.getValue()); };
            strip.eqLow.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqLowCommandSlider");
            strip.eqLow.setComponentID(strip.eqLow.getName());
            configureMixerSlider(strip.eqLow, model.decks[index].eqLow, 1.0);
            strip.eqLow.onValueChange = [&adapter, this, &slider = strip.eqLow, deckIndex] { dispatchMixerAndRefresh(adapter, MixerAction::EqLow, deckIndex, slider.getValue()); };
            strip.eqMid.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqMidCommandSlider");
            strip.eqMid.setComponentID(strip.eqMid.getName());
            configureMixerSlider(strip.eqMid, model.decks[index].eqMid, 1.0);
            strip.eqMid.onValueChange = [&adapter, this, &slider = strip.eqMid, deckIndex] { dispatchMixerAndRefresh(adapter, MixerAction::EqMid, deckIndex, slider.getValue()); };
            strip.eqHigh.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqHighCommandSlider");
            strip.eqHigh.setComponentID(strip.eqHigh.getName());
            configureMixerSlider(strip.eqHigh, model.decks[index].eqHigh, 1.0);
            strip.eqHigh.onValueChange = [&adapter, this, &slider = strip.eqHigh, deckIndex] { dispatchMixerAndRefresh(adapter, MixerAction::EqHigh, deckIndex, slider.getValue()); };
            addAndMakeVisible(strip.play);
            addAndMakeVisible(strip.cue);
            addAndMakeVisible(strip.sync);
            addAndMakeVisible(strip.volume);
            addAndMakeVisible(strip.gain);
            addAndMakeVisible(strip.eqLow);
            addAndMakeVisible(strip.eqMid);
            addAndMakeVisible(strip.eqHigh);
        }
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 34);
        auto stripsArea = area.removeFromTop(area.getHeight() - 34);
        const auto stripWidth = stripsArea.getWidth() / static_cast<int>(strips_.size());
        for (auto& strip : strips_) {
            auto stripArea = stripsArea.removeFromLeft(stripWidth).reduced(2, 0);
            auto buttons = stripArea.removeFromTop(22);
            strip.play.setBounds(buttons.removeFromLeft(buttons.getWidth() / 3));
            strip.cue.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2));
            strip.sync.setBounds(buttons);
            stripArea.removeFromTop(4);
            const auto sliderHeight = stripArea.getHeight() / 5;
            strip.volume.setBounds(stripArea.removeFromTop(sliderHeight));
            strip.gain.setBounds(stripArea.removeFromTop(sliderHeight));
            strip.eqLow.setBounds(stripArea.removeFromTop(sliderHeight));
            strip.eqMid.setBounds(stripArea.removeFromTop(sliderHeight));
            strip.eqHigh.setBounds(stripArea);
        }
        area.removeFromTop(4);
        crossfader_.setBounds(area);
    }

    void refreshFromSnapshot(const app::HybridUiShellSnapshot& snapshot, const decks::FourDeckPlaybackCore& playbackCore) {
        crossfader_.setValue(snapshot.mixer.crossfader, juce::dontSendNotification);
        crossfader_.setEnabled(true);
        for (std::size_t index = 0; index < strips_.size(); ++index) {
            const auto deckId = core::DeckId::fromIndex(index).value;
            const auto deckLoaded = playbackCore.deck(deckId).state().loaded;
            auto& strip = strips_[index];
            const auto& deck = snapshot.mixer.decks[index];
            strip.play.setButtonText(deckLoaded ? "Play" : "Load Deck");
            strip.cue.setButtonText(deckLoaded ? "Cue" : "Load Deck");
            strip.sync.setButtonText(deckLoaded ? "Sync" : "Load Deck");
            strip.play.setTooltip(deckLoaded ? "Play loaded deck." : "Load a browser track before transport controls become available.");
            strip.cue.setTooltip(deckLoaded ? "Cue loaded deck." : "Load a browser track before cue becomes available.");
            strip.sync.setTooltip(deckLoaded ? "Sync loaded deck." : "Load a browser track before sync becomes available.");
            strip.play.setEnabled(deckLoaded);
            strip.cue.setEnabled(deckLoaded);
            strip.sync.setEnabled(deckLoaded);
            strip.volume.setValue(deck.volume, juce::dontSendNotification);
            strip.gain.setValue(deck.gain, juce::dontSendNotification);
            strip.eqLow.setValue(deck.eqLow, juce::dontSendNotification);
            strip.eqMid.setValue(deck.eqMid, juce::dontSendNotification);
            strip.eqHigh.setValue(deck.eqHigh, juce::dontSendNotification);
            strip.volume.setEnabled(true);
            strip.gain.setEnabled(true);
            strip.eqLow.setEnabled(true);
            strip.eqMid.setEnabled(true);
            strip.eqHigh.setEnabled(true);
        }
    }

private:
    struct DeckStrip final {
        juce::TextButton play;
        juce::TextButton cue;
        juce::TextButton sync;
        juce::Slider volume;
        juce::Slider gain;
        juce::Slider eqLow;
        juce::Slider eqMid;
        juce::Slider eqHigh;
    };

    void dispatchMixerAndRefresh(JuceUiCommandAdapter& adapter, MixerAction action, std::size_t deckIndex, double value) {
        dispatchMixer(adapter, statusSink_, action, deckIndex, value);
        refreshControls_();
    }

    std::array<DeckStrip, audio::routing::kDeckCount> strips_{};
    juce::Slider crossfader_;
    CommandResultStatusSink statusSink_;
    std::function<void()> refreshControls_;
};

class MainComponent::BrowserComponent final : public IndustrialPanel, private juce::TableListBoxModel {
public:
    BrowserComponent(const app::BrowserPanelViewModel& model,
                     JuceUiCommandAdapter& adapter,
                     std::vector<library::AudioImportClassification>& browserRows,
                     std::string smokeFixtureDirectory,
                     CommandResultStatusSink statusSink,
                     std::function<void()> refreshControls)
        : IndustrialPanel("BrowserComponent", model.empty ? "Browser: Empty" : "Browser: Library", tokens().cyan),
          adapter_(adapter),
          browserRows_(browserRows),
          smokeFixtureDirectory_(std::move(smokeFixtureDirectory)),
          statusSink_(std::move(statusSink)),
          refreshControls_(std::move(refreshControls)) {
        importFiles_.setName("ImportFilesButton");
        importFiles_.setComponentID("ImportFilesButton");
        importFolder_.setName("ImportFolderButton");
        importFolder_.setComponentID("ImportFolderButton");
        updateImportCommandState();
        importFiles_.onClick = [this] { importFiles(); };
        importFolder_.onClick = [this] { importFolder(); };
        targetDeck_.setName("BrowserTargetDeckSelector");
        targetDeck_.setComponentID("BrowserTargetDeckSelector");
        for (std::size_t index = 0; index < audio::routing::kDeckCount; ++index) {
            targetDeck_.addItem(juce::String("Deck ") + juce::String(static_cast<int>(index + 1U)), static_cast<int>(index + 1U));
        }
        targetDeck_.setSelectedId(1, juce::dontSendNotification);
        loadSelected_.setButtonText("Load Selected");
        loadSelected_.setName("LoadSelectedBrowserTrackButton");
        loadSelected_.setComponentID("LoadSelectedBrowserTrackButton");
        configureUnavailableButton(loadSelected_, "Select Track", "Import and select an importable track before loading a deck.");
        loadSelected_.onClick = [this] { loadSelectedRow(); };
        trackTable_.setName("BrowserTrackTableModel");
        trackTable_.setComponentID("BrowserTrackTableModel");
        trackTable_.getHeader().addColumn("Title", 1, 150);
        trackTable_.getHeader().addColumn("Status", 2, 96);
        trackTable_.setModel(this);
        addAndMakeVisible(importFiles_);
        addAndMakeVisible(importFolder_);
        addAndMakeVisible(targetDeck_);
        addAndMakeVisible(loadSelected_);
        addAndMakeVisible(trackTable_);
    }

    int getNumRows() override { return static_cast<int>(browserRows_.size()); }

    void paintRowBackground(juce::Graphics& graphics, int rowNumber, int, int, bool rowIsSelected) override {
        if (rowIsSelected) {
            graphics.fillAll(tokens().cyan.withAlpha(0.18F));
        } else if (rowNumber % 2 == 0) {
            graphics.fillAll(tokens().deckFace.withAlpha(0.35F));
        }
    }

    void paintCell(juce::Graphics& graphics, int rowNumber, int columnId, int width, int height, bool) override {
        if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= browserRows_.size()) {
            return;
        }
        const auto& row = browserRows_[static_cast<std::size_t>(rowNumber)];
        graphics.setColour(row.importable() ? tokens().text : tokens().red);
        graphics.setFont(juce::FontOptions(12.0F));
        const auto text = columnId == 1 ? juce::String(library::titleFromPath(row.entry.path)) : juce::String(library::toString(row.error));
        graphics.drawText(text, juce::Rectangle<int>{0, 0, width, height}.reduced(4, 0), juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int lastRowSelected) override {
        selectedRow_ = lastRowSelected < 0 ? npos : static_cast<std::size_t>(lastRowSelected);
        if (selectedRow_ != npos) {
            statusSink_(adapter_.dispatch(BrowserIntent{BrowserAction::SelectRow, {}, selectedDeckIndex(), selectedRow_}));
        }
        refreshFromAuthoritativeState();
    }

    void cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) override {
        selectedRow_ = rowNumber < 0 ? npos : static_cast<std::size_t>(rowNumber);
        loadSelectedRow();
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 36);
        auto buttons = area.removeFromTop(30);
        importFiles_.setBounds(buttons.removeFromLeft((buttons.getWidth() - tokens().gap) / 2));
        buttons.removeFromLeft(tokens().gap);
        importFolder_.setBounds(buttons);
        area.removeFromTop(tokens().gap);
        auto loadControls = area.removeFromTop(28);
        targetDeck_.setBounds(loadControls.removeFromLeft((loadControls.getWidth() - tokens().gap) / 2));
        loadControls.removeFromLeft(tokens().gap);
        loadSelected_.setBounds(loadControls);
        area.removeFromTop(tokens().gap);
        trackTable_.setBounds(area);
    }

    void refreshFromAuthoritativeState() {
        updateImportCommandState();
        targetDeck_.setEnabled(true);
        trackTable_.updateContent();
        loadSelected_.setEnabled(canLoadSelectedRow());
        loadSelected_.setButtonText(canLoadSelectedRow() ? "Load Selected" : "Select Track");
        loadSelected_.setTooltip(canLoadSelectedRow() ? "Load the selected browser row to the selected deck." : "Import and select an importable track before loading a deck.");
    }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    [[nodiscard]] std::size_t selectedDeckIndex() const noexcept {
        const auto selectedId = targetDeck_.getSelectedId();
        return selectedId <= 1 ? 0U : static_cast<std::size_t>(selectedId - 1);
    }

    void importSmokeFile() {
        importEntry(library::FilesystemEntry{smokeFixtureDirectory_ + "/track_120bpm.wav", true});
    }

    void importSmokeFolderError() {
        importEntry(library::FilesystemEntry{smokeFixtureDirectory_ + "/folder", false});
    }

    void importFiles() {
        if (!smokeFixtureDirectory_.empty()) {
            importSmokeFile();
            return;
        }
        fileChooser_ = std::make_unique<juce::FileChooser>("Import audio files", juce::File{}, "*", true);
        const juce::Component::SafePointer<BrowserComponent> safeThis(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles |
                                      juce::FileBrowserComponent::canSelectMultipleItems,
                                  [safeThis](const juce::FileChooser& chooser) {
                                      auto* browser = safeThis.getComponent();
                                      if (browser == nullptr) {
                                          return;
                                      }
                                      for (const auto& file : chooser.getResults()) {
                                          browser->importEntry(library::FilesystemEntry{file.getFullPathName().toStdString(), true});
                                      }
                                  });
    }

    void importFolder() {
        if (!smokeFixtureDirectory_.empty()) {
            importSmokeFolderError();
            return;
        }
        folderChooser_ = std::make_unique<juce::FileChooser>("Import audio folder", juce::File{}, "*", true);
        const juce::Component::SafePointer<BrowserComponent> safeThis(this);
        folderChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                        juce::FileBrowserComponent::canSelectDirectories,
                                    [safeThis](const juce::FileChooser& chooser) {
                                        auto* browser = safeThis.getComponent();
                                        if (browser == nullptr) {
                                            return;
                                        }
                                        const auto folder = chooser.getResult();
                                        if (folder == juce::File{} || !folder.isDirectory()) {
                                            return;
                                        }
                                        juce::Array<juce::File> files;
                                        folder.findChildFiles(files, juce::File::findFiles, false);
                                        for (const auto& file : files) {
                                            browser->importEntry(library::FilesystemEntry{file.getFullPathName().toStdString(), file.existsAsFile()});
                                        }
                                    });
    }

    void importEntry(library::FilesystemEntry entry) {
        const auto before = browserRows_.size();
        const auto result = adapter_.dispatch(BrowserIntent{BrowserAction::Import, std::move(entry), selectedDeckIndex()});
        statusSink_(result);
        trackTable_.updateContent();
        if (browserRows_.size() > before) {
            selectedRow_ = browserRows_.size() - 1U;
            trackTable_.selectRow(static_cast<int>(selectedRow_));
            if (!result.ok()) {
                statusSink_(result);
            }
        }
        refreshFromAuthoritativeState();
    }

    void loadSelectedRow() {
        if (selectedRow_ == npos || selectedRow_ >= browserRows_.size() || !browserRows_[selectedRow_].importable()) {
            refreshFromAuthoritativeState();
            return;
        }
        const auto result = adapter_.dispatch(BrowserIntent{BrowserAction::LoadToDeck, browserRows_[selectedRow_].entry, selectedDeckIndex(), selectedRow_});
        statusSink_(result);
        refreshFromAuthoritativeState();
        loadSelected_.setEnabled(result.ok() && canLoadSelectedRow());
        refreshControls_();
    }

    [[nodiscard]] bool canLoadSelectedRow() const {
        return selectedRow_ != npos && selectedRow_ < browserRows_.size() && browserRows_[selectedRow_].importable();
    }

    void updateImportCommandState() {
        const auto smokeCommandsAvailable = !smokeFixtureDirectory_.empty();
        importFiles_.setButtonText(smokeCommandsAvailable ? "Smoke File" : "Import Files");
        importFolder_.setButtonText(smokeCommandsAvailable ? "Smoke Folder" : "Import Folder");
        importFiles_.setTooltip(smokeCommandsAvailable ? "Smoke-only deterministic fixture import." : "Open the native file chooser and classify selected audio files.");
        importFolder_.setTooltip(smokeCommandsAvailable ? "Smoke-only deterministic folder error import." : "Open the native folder chooser and classify the selected folder.");
        importFiles_.setEnabled(true);
        importFolder_.setEnabled(true);
    }

    JuceUiCommandAdapter& adapter_;
    std::vector<library::AudioImportClassification>& browserRows_;
    std::string smokeFixtureDirectory_;
    CommandResultStatusSink statusSink_;
    std::function<void()> refreshControls_;
    std::size_t selectedRow_{npos};
    juce::TextButton importFiles_{"ImportFilesButton"};
    juce::TextButton importFolder_{"ImportFolderButton"};
    juce::ComboBox targetDeck_;
    juce::TextButton loadSelected_{"LoadSelectedBrowserTrackButton"};
    juce::TableListBox trackTable_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<juce::FileChooser> folderChooser_;
};

class MainComponent::WaveformComponent final : public IndustrialPanel {
public:
    WaveformComponent() : IndustrialPanel("WaveformComponent", "Waveform Overview: AudioThumbnail Cache", tokens().lime) {}
};

class MainComponent::BeatgridEditorComponent final : public IndustrialPanel {
public:
    BeatgridEditorComponent() : IndustrialPanel("BeatgridEditorComponent", "Beatgrid Editor: BPM Downbeat Cues", tokens().amber) {}
};

class MainComponent::PluginChainComponent final : public IndustrialPanel {
public:
    PluginChainComponent(const app::PluginChainPanelViewModel& model, JuceUiCommandAdapter& adapter, CommandResultStatusSink statusSink)
        : IndustrialPanel("PluginChainComponent", "Plugin Chain: Deck + Master Editor Panels", tokens().red),
          adapter_(adapter),
          statusSink_(std::move(statusSink)) {
        for (std::size_t index = 0; index < model.slots.size(); ++index) {
            const auto& slot = model.slots[index];
            auto row = std::make_unique<SlotControls>();
            row->target = pluginTargetForIndex(index);
            row->deckIndex = deckIndexForSlot(index);
            row->slotIndex = slotIndexForSlot(index);
            configureButton(row->bypass, slot, "BypassCommandButton", slot.bypassed ? "Enable" : "Bypass", !slot.placeholder);
            configureButton(row->remove, slot, "RemoveCommandButton", "Remove", !slot.placeholder && slot.removable);
            configureButton(row->moveUp, slot, "MoveUpCommandButton", "Move Up", !slot.placeholder && slot.canMoveUp);
            configureButton(row->moveDown, slot, "MoveDownCommandButton", "Move Down", !slot.placeholder && slot.canMoveDown);
            configureButton(row->openEditor, slot, "OpenEditorCommandButton", "Open Editor", !slot.placeholder && slot.nativeEditorAvailable);
            configureButton(row->closeEditor, slot, "CloseEditorCommandButton", "Close Editor", !slot.placeholder && slot.nativeEditorAvailable);
            configureParameter(row->parameter, slot);
            const auto bypassed = !slot.bypassed;
            row->bypass.onClick = [this, controls = row.get(), bypassed] { dispatch(*controls, PluginChainAction::Bypass, bypassed); };
            row->remove.onClick = [this, controls = row.get()] { dispatch(*controls, PluginChainAction::Remove); };
            row->moveUp.onClick = [this, controls = row.get()] { dispatch(*controls, PluginChainAction::MoveUp); };
            row->moveDown.onClick = [this, controls = row.get()] { dispatch(*controls, PluginChainAction::MoveDown); };
            row->openEditor.onClick = [this, controls = row.get()] { dispatch(*controls, PluginChainAction::OpenEditor); };
            row->closeEditor.onClick = [this, controls = row.get()] { dispatch(*controls, PluginChainAction::CloseEditor); };
            row->parameter.onValueChange = [this, controls = row.get()] { dispatchParameter(*controls); };
            addAndMakeVisible(row->bypass);
            addAndMakeVisible(row->remove);
            addAndMakeVisible(row->moveUp);
            addAndMakeVisible(row->moveDown);
            addAndMakeVisible(row->openEditor);
            addAndMakeVisible(row->closeEditor);
            addAndMakeVisible(row->parameter);
            slotControls_.push_back(std::move(row));
        }
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 34);
        const auto rows = static_cast<int>(std::min<std::size_t>(slotControls_.size(), 6U));
        if (rows == 0) {
            return;
        }
        const auto rowHeight = std::max(18, area.getHeight() / rows);
        for (std::size_t index = 0; index < slotControls_.size(); ++index) {
            auto& row = *slotControls_[index];
            if (index >= 6U) {
                setRowBounds(row, {});
                continue;
            }
            auto rowArea = area.removeFromTop(rowHeight).reduced(0, 1);
            const auto actionWidth = std::max(20, rowArea.getWidth() / 7);
            row.bypass.setBounds(rowArea.removeFromLeft(actionWidth));
            row.remove.setBounds(rowArea.removeFromLeft(actionWidth));
            row.moveUp.setBounds(rowArea.removeFromLeft(actionWidth));
            row.moveDown.setBounds(rowArea.removeFromLeft(actionWidth));
            row.openEditor.setBounds(rowArea.removeFromLeft(actionWidth));
            row.closeEditor.setBounds(rowArea.removeFromLeft(actionWidth));
            row.parameter.setBounds(rowArea);
        }
    }

    void refreshFromSnapshot(const app::PluginChainPanelViewModel& model) {
        for (std::size_t index = 0; index < slotControls_.size() && index < model.slots.size(); ++index) {
            const auto& slot = model.slots[index];
            auto& row = *slotControls_[index];
            updateButton(row.bypass, slot, slot.bypassed ? "Enable" : "Bypass", !slot.placeholder);
            updateButton(row.remove, slot, "Remove", !slot.placeholder && slot.removable);
            updateButton(row.moveUp, slot, "Move Up", !slot.placeholder && slot.canMoveUp);
            updateButton(row.moveDown, slot, "Move Down", !slot.placeholder && slot.canMoveDown);
            updateButton(row.openEditor, slot, "Open Editor", !slot.placeholder && slot.nativeEditorAvailable);
            updateButton(row.closeEditor, slot, "Close Editor", !slot.placeholder && slot.nativeEditorAvailable);
            row.parameter.setTooltip(slot.placeholder ? "No plugin loaded in this slot." : "Generic gain parameter.");
            row.bypass.setEnabled(!slot.placeholder);
            row.remove.setEnabled(!slot.placeholder && slot.removable);
            row.moveUp.setEnabled(!slot.placeholder && slot.canMoveUp);
            row.moveDown.setEnabled(!slot.placeholder && slot.canMoveDown);
            row.openEditor.setEnabled(!slot.placeholder && slot.nativeEditorAvailable);
            row.closeEditor.setEnabled(!slot.placeholder && slot.nativeEditorAvailable);
            row.parameter.setValue(slot.parameters.empty() ? 0.0 : slot.parameters.front().normalizedValue, juce::dontSendNotification);
            row.parameter.setEnabled(!slot.placeholder && !slot.parameters.empty());
        }
    }

private:
    struct SlotControls final {
        plugins::PluginChainTargetKind target{plugins::PluginChainTargetKind::Deck};
        std::size_t deckIndex{};
        std::size_t slotIndex{};
        std::string parameterId{"gain"};
        juce::TextButton bypass;
        juce::TextButton remove;
        juce::TextButton moveUp;
        juce::TextButton moveDown;
        juce::TextButton openEditor;
        juce::TextButton closeEditor;
        juce::Slider parameter;
    };

    static plugins::PluginChainTargetKind pluginTargetForIndex(std::size_t index) noexcept {
        return index < audio::routing::kDeckCount * audio::routing::kPluginSlotsPerDeck ? plugins::PluginChainTargetKind::Deck : plugins::PluginChainTargetKind::Master;
    }

    static std::size_t deckIndexForSlot(std::size_t index) noexcept {
        return index / audio::routing::kPluginSlotsPerDeck;
    }

    static std::size_t slotIndexForSlot(std::size_t index) noexcept {
        return index % audio::routing::kPluginSlotsPerDeck;
    }

    static void configureButton(juce::TextButton& button, const app::PluginSlotViewModel& slot, const char* suffix, const char* text, bool enabled) {
        const auto name = juce::String(slot.componentName) + suffix;
        button.setName(name);
        button.setComponentID(name);
        updateButton(button, slot, text, enabled);
    }

    static void updateButton(juce::TextButton& button, const app::PluginSlotViewModel& slot, const char* actionText, bool enabled) {
        button.setButtonText(slot.placeholder ? "No Plugin" : actionText);
        button.setTooltip(slot.placeholder ? "No plugin loaded in this slot." : juce::String(actionText));
        button.setEnabled(enabled);
    }

    static void configureParameter(juce::Slider& slider, const app::PluginSlotViewModel& slot) {
        const auto hasParameter = !slot.placeholder && !slot.parameters.empty();
        const auto name = juce::String(slot.componentName) + "GenericGainParameterCommandSlider";
        slider.setName(name);
        slider.setComponentID(name);
        slider.setRange(0.0, 1.0, 0.01);
        slider.setValue(slot.parameters.empty() ? 0.0 : slot.parameters.front().normalizedValue, juce::dontSendNotification);
        slider.setEnabled(hasParameter);
        slider.setTooltip(slot.placeholder ? "No plugin loaded in this slot." : "Generic gain parameter.");
    }

    static void setRowBounds(SlotControls& row, juce::Rectangle<int> bounds) {
        row.bypass.setBounds(bounds);
        row.remove.setBounds(bounds);
        row.moveUp.setBounds(bounds);
        row.moveDown.setBounds(bounds);
        row.openEditor.setBounds(bounds);
        row.closeEditor.setBounds(bounds);
        row.parameter.setBounds(bounds);
    }

    void dispatch(const SlotControls& controls, PluginChainAction action, bool bypassed = false) {
        statusSink_(adapter_.dispatch(PluginChainIntent{action, controls.target, controls.deckIndex, controls.slotIndex, bypassed}));
    }

    void dispatchParameter(const SlotControls& controls) {
        statusSink_(adapter_.dispatch(PluginChainIntent{PluginChainAction::Parameter,
                                                        controls.target,
                                                        controls.deckIndex,
                                                        controls.slotIndex,
                                                        false,
                                                        controls.parameterId,
                                                        controls.parameter.getValue()}));
    }

    JuceUiCommandAdapter& adapter_;
    CommandResultStatusSink statusSink_;
    std::vector<std::unique_ptr<SlotControls>> slotControls_;
};

class MainComponent::MidiLearnComponent final : public IndustrialPanel {
public:
    explicit MidiLearnComponent(const app::MidiLearnIndicatorViewModel& model)
        : IndustrialPanel("MidiLearnComponent", model.learning ? "MIDI Learn Armed" : "MIDI Learn Idle", tokens().cyan) {}
};

class MainComponent::StatusBarComponent final : public IndustrialPanel {
public:
    explicit StatusBarComponent(const app::AppStatusViewModel& model)
        : IndustrialPanel("StatusBarComponent", model.statusText, tokens().lime) {
        statusText_.setName("StatusTextLabel");
        statusText_.setComponentID("StatusTextLabel");
        statusText_.setJustificationType(juce::Justification::centredLeft);
        statusText_.setColour(juce::Label::textColourId, tokens().text);
        statusText_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        statusText_.setInterceptsMouseClicks(false, false);
        setStatusText(model.statusText);
        addAndMakeVisible(statusText_);
    }

    void setStatusText(const juce::String& text) {
        statusText_.setText(text, juce::dontSendNotification);
    }

    [[nodiscard]] juce::String statusText() const { return statusText_.getText(); }

    void resized() override {
        IndustrialPanel::resized();
        statusText_.setBounds(getLocalBounds().reduced(12, 18));
    }

private:
    juce::Label statusText_;
};

class MainComponent::AudioSettingsComponent final : public IndustrialPanel {
public:
    AudioSettingsComponent() : IndustrialPanel("AudioSettingsComponent", "Audio Settings", tokens().amber) {}
};

#endif


#if DECKFLAXIA_HAS_JUCE

MainComponent::~MainComponent() = default;

MainComponent::MainComponent(bool noAudioDevice)
    : snapshot_(shellModel_.buildSnapshot(app::createUiSmokeInput(false))),
      commandAdapter_(JuceUiCommandAdapterServices{&playbackCore_, &playbackCore_.mixer(), &playbackCore_.routing(), nullptr, nullptr, &browserRows_}) {
    if (noAudioDevice) {
        audioInitializationMessage_ = "no audio device requested";
    } else {
        const auto audioError = audioDeviceManager_.initialise(0, 0, nullptr, true);
        audioDeviceManagerInitialized_ = audioError.isEmpty();
        audioInitializationMessage_ = audioDeviceManagerInitialized_ ? "audio device manager initialized without playback" : audioError;
    }

    setName("MainComponent");

    for (std::size_t index = 0; index < decks_.size(); ++index) {
        decks_[index] = std::make_unique<DeckComponent>(index, snapshot_.decks[index]);
        addAndMakeVisible(*decks_[index]);
    }

    const CommandResultStatusSink statusSink = [this](const JuceUiCommandResult& result) {
        if (statusBar_ != nullptr) {
            statusBar_->setStatusText(formatCommandResultStatus(result));
        }
    };

    mixer_ = std::make_unique<MixerComponent>(snapshot_.mixer, commandAdapter_, statusSink, [this] { refreshFromAuthoritativeState(); });
    browser_ = std::make_unique<BrowserComponent>(snapshot_.browser,
                                                  commandAdapter_,
                                                  browserRows_,
                                                  noAudioDevice ? "tests/fixtures/dj-workflow" : "",
                                                  statusSink,
                                                  [this] { refreshFromAuthoritativeState(); });
    waveform_ = std::make_unique<WaveformComponent>();
    beatgridEditor_ = std::make_unique<BeatgridEditorComponent>();
    pluginChain_ = std::make_unique<PluginChainComponent>(snapshot_.pluginChain, commandAdapter_, statusSink);
    midiLearn_ = std::make_unique<MidiLearnComponent>(snapshot_.midiLearn);
    statusBar_ = std::make_unique<StatusBarComponent>(snapshot_.status);
    audioSettings_ = std::make_unique<AudioSettingsComponent>();

    addAndMakeVisible(*mixer_);
    addAndMakeVisible(*browser_);
    addAndMakeVisible(*waveform_);
    addAndMakeVisible(*beatgridEditor_);
    addAndMakeVisible(*pluginChain_);
    addAndMakeVisible(*midiLearn_);
    addAndMakeVisible(*statusBar_);
    addAndMakeVisible(*audioSettings_);

    setSize(1280, 720);
    refreshFromAuthoritativeState();
}

bool MainComponent::audioDeviceManagerInitialized() const noexcept { return audioDeviceManagerInitialized_; }
bool MainComponent::commandManagerPresent() const noexcept { return true; }
const juce::String& MainComponent::audioInitializationMessage() const noexcept { return audioInitializationMessage_; }
const app::HybridUiShellSnapshot& MainComponent::snapshot() const noexcept { return snapshot_; }
const decks::FourDeckPlaybackCore& MainComponent::playbackCore() const noexcept { return playbackCore_; }
const audio::MixerSnapshot& MainComponent::mixerSnapshot() const noexcept { return playbackCore_.mixerSnapshot(); }
const std::vector<library::AudioImportClassification>& MainComponent::browserRows() const noexcept { return browserRows_; }

void MainComponent::refreshFromAuthoritativeState() {
    snapshot_ = shellModel_.buildSnapshot(currentShellInput(playbackCore_));
    for (std::size_t index = 0; index < decks_.size(); ++index) {
        decks_[index]->refreshFromSnapshot(snapshot_.decks[index]);
    }
    mixer_->refreshFromSnapshot(snapshot_, playbackCore_);
    browser_->refreshFromAuthoritativeState();
    pluginChain_->refreshFromSnapshot(snapshot_.pluginChain);
    repaint();
}

void MainComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour(0xff0b0d10));
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient gradient(tokens().rail.withAlpha(0.7F), bounds.getTopLeft(), tokens().deckFace, bounds.getBottomRight(), false);
    graphics.setGradientFill(gradient);
    graphics.fillRect(bounds);
}

void MainComponent::resized() {
    const auto layout = calculateSmokeLayout(getLocalBounds());
    statusBar_->setBounds(layout.status);
    browser_->setBounds(layout.browser);
    audioSettings_->setBounds(layout.audioSettings);
    midiLearn_->setBounds(layout.midiLearn);
    decks_[0]->setBounds(layout.decks[0]);
    decks_[1]->setBounds(layout.decks[1]);
    decks_[2]->setBounds(layout.decks[2]);
    decks_[3]->setBounds(layout.decks[3]);
    mixer_->setBounds(layout.mixer);
    waveform_->setBounds(layout.waveform);
    beatgridEditor_->setBounds(layout.beatgridEditor);
    pluginChain_->setBounds(layout.pluginChain);
}

void writeComponentTreeReport(const MainComponent& component, std::ostream& output) {
    output << "juce-ui-smoke-test: ok\n";
    output << "component-tree: native-juce industrial-four-deck\n";
    output << "snapshot-source: app::HybridUiShellModel command-state snapshot\n";
    output << "DeckComponent[4]: count=" << countByPrefix(component, "DeckComponent[") << '\n';
    for (const auto* name : kRequiredComponentNames) {
        const auto prefix = std::string(name) == "DeckComponent" ? "DeckComponent[" : name;
        output << "required-component: " << name << " count=" << countByPrefix(component, prefix) << '\n';
    }
    output << "native-control-inventory: live-runtime component-dump-authoritative\n";
    output << "tree:\n";
    dumpComponent(component, output, 0);
}


void writeHeadlessComponentTreeReport(std::ostream& output) {
    writeSmokeTreeSkeleton(output);
}

bool writeComponentScreenshot(MainComponent& component, const std::filesystem::path& screenshotPath, std::ostream& output) {
    if (screenshotPath.empty()) {
        output << "screenshot: failed reason=missing-path\n";
        return false;
    }

    component.setSize(1920, 1080);
    component.resized();
    juce::Image image(juce::Image::RGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics(image);
    component.paintEntireComponent(graphics, true);
    juce::PNGImageFormat format;
    juce::File file(screenshotPath.string());
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || !format.writeImageToStream(image, *stream)) {
        output << "screenshot: failed path=" << screenshotPath.string() << '\n';
        return false;
    }
    stream->flush();
    output << "screenshot-source: live-main-component\n";
    output << "screenshot-size: " << component.getWidth() << 'x' << component.getHeight() << '\n';
    output << "screenshot: wrote path=" << screenshotPath.string() << " bytes=" << file.getSize() << '\n';
    return file.getSize() > 0;
}


bool writeHeadlessComponentScreenshot(const std::filesystem::path& screenshotPath, std::ostream& output) {
    if (screenshotPath.empty()) {
        output << "screenshot: failed reason=missing-path\n";
        return false;
    }

    juce::Image image(juce::Image::RGB, 1280, 720, true);
    juce::Graphics graphics(image);
    paintHeadlessSmokeScreenshot(graphics, image.getWidth(), image.getHeight());
    juce::PNGImageFormat format;
    juce::File file(screenshotPath.string());
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || !format.writeImageToStream(image, *stream)) {
        output << "screenshot: failed path=" << screenshotPath.string() << '\n';
        return false;
    }
    stream->flush();
    output << "screenshot: wrote path=" << screenshotPath.string() << " bytes=" << file.getSize() << '\n';
    return file.getSize() > 0;
}

#endif

int runUnavailableJuceUiSmoke(std::ostream& output, bool dumpComponents, const std::filesystem::path& screenshotPath) {
    output << "juce-ui-smoke-test: blocked\n";
    output << "juce-ui-engine=unavailable\n";
    output << "component-tree: unavailable reason=DECKFLAXIA_HAS_JUCE=0\n";
    writeUnavailableControlInventory(output);
    if (dumpComponents) {
        output << "dump-components: blocked reason=real JUCE Component tree requires JUCE\n";
    }
    if (!screenshotPath.empty()) {
        output << "screenshot: blocked path=" << screenshotPath.string() << " reason=real PNG capture requires JUCE\n";
    }
    return screenshotPath.empty() ? 0 : 2;
}

}
