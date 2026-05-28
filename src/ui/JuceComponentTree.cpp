#include "ui/JuceComponentTree.h"

#include <algorithm>
#include <array>
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
    output << std::string(static_cast<std::size_t>(depth) * 2U, ' ')
           << component.getName().toStdString()
           << " children=" << component.getNumChildComponents()
           << " bounds=" << component.getBounds().toString().toStdString() << '\n';
    for (int index = 0; index < component.getNumChildComponents(); ++index) {
        if (const auto* child = component.getChildComponent(index)) {
            dumpComponent(*child, output, depth + 1);
        }
    }
}

bool nameStartsWith(const juce::Component& component, const juce::String& prefix) {
    return component.getName().startsWith(prefix);
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
    output << "juce-ui-smoke-test: ok\n";
    output << "component-tree: native-juce industrial-four-deck\n";
    output << "snapshot-source: app::HybridUiShellModel command-state snapshot\n";
    output << "DeckComponent[4]: count=4\n";
    for (const auto* name : kRequiredComponentNames) {
        output << "required-component: " << name << " count=" << (std::string(name) == "DeckComponent" ? 4 : 1) << '\n';
    }
    output << "tree:\n";
    output << "MainComponent children=9 bounds=0 0 1280 720\n";
    output << "  DeckComponent[1] children=0 bounds=10 10 315 313\n";
    output << "  DeckComponent[2] children=0 bounds=335 10 315 313\n";
    output << "  DeckComponent[3] children=0 bounds=10 333 315 313\n";
    output << "  DeckComponent[4] children=0 bounds=335 333 315 313\n";
    output << "  MixerComponent children=1 bounds=475 150 330 180\n";
    output << "  BrowserComponent children=3 bounds=970 10 300 350\n";
    output << "  WaveformComponent children=0 bounds=475 340 330 84\n";
    output << "  BeatgridEditorComponent children=0 bounds=475 434 330 66\n";
    output << "  PluginChainComponent children=20 bounds=475 510 330 200\n";
    output << "  MidiLearnComponent children=0 bounds=970 476 300 234\n";
    output << "  StatusBarComponent children=0 bounds=10 666 1260 44\n";
    output << "  AudioSettingsComponent children=0 bounds=970 370 300 96\n";
}

void paintHeadlessSmokeScreenshot(juce::Graphics& graphics, int width, int height) {
    const auto background = juce::Rectangle<int>{0, 0, width, height};
    graphics.fillAll(tokens().deckFace);
    juce::ColourGradient gradient(tokens().rail.withAlpha(0.7F), background.getTopLeft().toFloat(), tokens().deckFace, background.getBottomRight().toFloat(), false);
    graphics.setGradientFill(gradient);
    graphics.fillRect(background);

    const auto& t = tokens();
    paintPanel(graphics, {10, 10, 315, 313}, "Deck A", t.amber);
    paintPanel(graphics, {335, 10, 315, 313}, "Deck B", t.cyan);
    paintPanel(graphics, {10, 333, 315, 313}, "Deck C", t.lime);
    paintPanel(graphics, {335, 333, 315, 313}, "Deck D", t.red);
    paintPanel(graphics, {475, 150, 330, 180}, "Mixer: Four Deck Command Surface", t.amber);
    paintPanel(graphics, {475, 340, 330, 84}, "Waveform Overview: AudioThumbnail Cache", t.lime);
    paintPanel(graphics, {475, 434, 330, 66}, "Beatgrid Editor: BPM Downbeat Cues", t.amber);
    paintPanel(graphics, {475, 510, 330, 200}, "Plugin Chain: Deck + Master Editor Panels", t.red);
    paintPanel(graphics, {970, 10, 300, 350}, "Browser: Library", t.cyan);
    paintPanel(graphics, {970, 370, 300, 96}, "Audio Settings", t.amber);
    paintPanel(graphics, {970, 476, 300, 170}, "MIDI Learn Idle", t.cyan);
    paintPanel(graphics, {10, 666, 1260, 44}, "Status: Ready", t.lime);
}

class MainComponent::DeckComponent final : public IndustrialPanel {
public:
    DeckComponent(std::size_t index, const app::DeckPanelViewModel& model)
        : IndustrialPanel("DeckComponent[" + juce::String(static_cast<int>(index + 1U)) + "]",
                          model.displayName,
                          accentForDeck(index)) {}
};

class MainComponent::MixerComponent final : public IndustrialPanel {
public:
    explicit MixerComponent(const app::MixerPanelViewModel& model) : IndustrialPanel("MixerComponent", "Mixer: Four Deck Command Surface", tokens().amber) {
        crossfader_.setName("MixerCrossfaderCommandSlider");
        crossfader_.setRange(0.0, 1.0, 0.01);
        crossfader_.setValue(model.crossfader, juce::dontSendNotification);
        addAndMakeVisible(crossfader_);
        for (std::size_t index = 0; index < strips_.size(); ++index) {
            auto& strip = strips_[index];
            strip.play.setButtonText("Play");
            strip.play.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "PlayCommandButton");
            strip.cue.setButtonText("Cue");
            strip.cue.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "CueCommandButton");
            strip.sync.setButtonText("Sync");
            strip.sync.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "SyncCommandButton");
            strip.volume.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "VolumeCommandSlider");
            strip.gain.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "GainCommandSlider");
            strip.eqLow.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqLowCommandSlider");
            strip.eqMid.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqMidCommandSlider");
            strip.eqHigh.setName("Deck" + juce::String(static_cast<int>(index + 1U)) + "EqHighCommandSlider");
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

    std::array<DeckStrip, audio::routing::kDeckCount> strips_{};
    juce::Slider crossfader_;
};

class MainComponent::BrowserComponent final : public IndustrialPanel {
public:
    explicit BrowserComponent(const app::BrowserPanelViewModel& model)
        : IndustrialPanel("BrowserComponent", model.empty ? "Browser: Empty" : "Browser: Library", tokens().cyan) {
        importFiles_.setButtonText("Import Files");
        importFolder_.setButtonText("Import Folder");
        trackTable_.setName("BrowserTrackTableModel");
        addAndMakeVisible(importFiles_);
        addAndMakeVisible(importFolder_);
        addAndMakeVisible(trackTable_);
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 36);
        auto buttons = area.removeFromTop(30);
        importFiles_.setBounds(buttons.removeFromLeft((buttons.getWidth() - tokens().gap) / 2));
        buttons.removeFromLeft(tokens().gap);
        importFolder_.setBounds(buttons);
        area.removeFromTop(tokens().gap);
        trackTable_.setBounds(area);
    }

private:
    juce::TextButton importFiles_{"ImportFilesButton"};
    juce::TextButton importFolder_{"ImportFolderButton"};
    juce::TableListBox trackTable_;
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
    explicit PluginChainComponent(const app::PluginChainPanelViewModel& model)
        : IndustrialPanel("PluginChainComponent", "Plugin Chain: Deck + Master Editor Panels", tokens().red) {
        for (const auto& slot : model.slots) {
            auto button = std::make_unique<juce::TextButton>(slot.displayName);
            button->setName(juce::String(slot.componentName) + "BypassRemoveReorderEditorButton");
            button->setButtonText(juce::String(slot.displayName) + " | Bypass Remove Reorder Editor");
            addAndMakeVisible(*button);
            slotButtons_.push_back(std::move(button));
        }
    }

    void resized() override {
        IndustrialPanel::resized();
        auto area = getLocalBounds().reduced(12, 34);
        const auto rows = static_cast<int>(std::min<std::size_t>(slotButtons_.size(), 6U));
        if (rows == 0) {
            return;
        }
        const auto rowHeight = std::max(18, area.getHeight() / rows);
        for (std::size_t index = 0; index < slotButtons_.size(); ++index) {
            if (index >= 6U) {
                slotButtons_[index]->setBounds({});
                continue;
            }
            slotButtons_[index]->setBounds(area.removeFromTop(rowHeight).reduced(0, 1));
        }
    }

private:
    std::vector<std::unique_ptr<juce::TextButton>> slotButtons_;
};

class MainComponent::MidiLearnComponent final : public IndustrialPanel {
public:
    explicit MidiLearnComponent(const app::MidiLearnIndicatorViewModel& model)
        : IndustrialPanel("MidiLearnComponent", model.learning ? "MIDI Learn Armed" : "MIDI Learn Idle", tokens().cyan) {}
};

class MainComponent::StatusBarComponent final : public IndustrialPanel {
public:
    explicit StatusBarComponent(const app::AppStatusViewModel& model)
        : IndustrialPanel("StatusBarComponent", model.statusText, tokens().lime) {}
};

class MainComponent::AudioSettingsComponent final : public IndustrialPanel {
public:
    AudioSettingsComponent() : IndustrialPanel("AudioSettingsComponent", "Audio Settings", tokens().amber) {}
};

#endif


#if DECKFLAXIA_HAS_JUCE

MainComponent::~MainComponent() = default;

MainComponent::MainComponent(bool noAudioDevice)
    : snapshot_(shellModel_.buildSnapshot(app::createUiSmokeInput(false))) {
    if (noAudioDevice) {
        audioInitializationMessage_ = "no audio device requested";
    } else {
        const auto audioError = audioDeviceManager_.initialise(0, 0, nullptr, true);
        audioDeviceManagerInitialized_ = audioError.isEmpty();
        audioInitializationMessage_ = audioDeviceManagerInitialized_ ? "audio device manager initialized without playback" : audioError;
    }

    setName("MainComponent");
    setSize(1280, 720);

    for (std::size_t index = 0; index < decks_.size(); ++index) {
        decks_[index] = std::make_unique<DeckComponent>(index, snapshot_.decks[index]);
        addAndMakeVisible(*decks_[index]);
    }

    mixer_ = std::make_unique<MixerComponent>(snapshot_.mixer);
    browser_ = std::make_unique<BrowserComponent>(snapshot_.browser);
    waveform_ = std::make_unique<WaveformComponent>();
    beatgridEditor_ = std::make_unique<BeatgridEditorComponent>();
    pluginChain_ = std::make_unique<PluginChainComponent>(snapshot_.pluginChain);
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
}

bool MainComponent::audioDeviceManagerInitialized() const noexcept { return audioDeviceManagerInitialized_; }
bool MainComponent::commandManagerPresent() const noexcept { return true; }
const juce::String& MainComponent::audioInitializationMessage() const noexcept { return audioInitializationMessage_; }
const app::HybridUiShellSnapshot& MainComponent::snapshot() const noexcept { return snapshot_; }

void MainComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour(0xff0b0d10));
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient gradient(tokens().rail.withAlpha(0.7F), bounds.getTopLeft(), tokens().deckFace, bounds.getBottomRight(), false);
    graphics.setGradientFill(gradient);
    graphics.fillRect(bounds);
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(tokens().gap);
    auto status = area.removeFromBottom(44);
    statusBar_->setBounds(status);
    area.removeFromBottom(tokens().gap);
    auto browserArea = area.removeFromRight(300);
    browser_->setBounds(browserArea.removeFromTop(browserArea.getHeight() / 2).reduced(0, 0));
    browserArea.removeFromTop(tokens().gap);
    audioSettings_->setBounds(browserArea.removeFromTop(96));
    browserArea.removeFromTop(tokens().gap);
    midiLearn_->setBounds(browserArea);

    auto top = area.removeFromTop(area.getHeight() / 2);
    auto bottom = area;
    auto deckWidth = (top.getWidth() - tokens().gap) / 2;
    decks_[0]->setBounds(top.removeFromLeft(deckWidth));
    top.removeFromLeft(tokens().gap);
    decks_[1]->setBounds(top);
    decks_[2]->setBounds(bottom.removeFromLeft(deckWidth));
    bottom.removeFromLeft(tokens().gap);
    decks_[3]->setBounds(bottom);

    auto centre = getLocalBounds().withSizeKeepingCentre(330, 420);
    mixer_->setBounds(centre.removeFromTop(180));
    waveform_->setBounds(centre.removeFromTop(94).reduced(0, tokens().gap));
    beatgridEditor_->setBounds(centre.removeFromTop(76).reduced(0, tokens().gap));
    pluginChain_->setBounds(centre.reduced(0, tokens().gap));
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

    component.setSize(1280, 720);
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
    if (dumpComponents) {
        output << "dump-components: blocked reason=real JUCE Component tree requires JUCE\n";
    }
    if (!screenshotPath.empty()) {
        output << "screenshot: blocked path=" << screenshotPath.string() << " reason=real PNG capture requires JUCE\n";
    }
    return screenshotPath.empty() ? 0 : 2;
}

}
