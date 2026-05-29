#include "app/UiShell.h"
#include "ui/JuceComponentTree.h"

#include <iostream>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

bool contains(const std::string& text, const std::string& expected) {
    return text.find(expected) != std::string::npos;
}

std::string lineContaining(const std::string& text, const std::string& needle) {
    const auto position = text.find(needle);
    if (position == std::string::npos) {
        return {};
    }
    const auto start = text.rfind('\n', position);
    const auto end = text.find('\n', position);
    return text.substr(start == std::string::npos ? 0U : start + 1U,
                       end == std::string::npos ? std::string::npos : end - (start == std::string::npos ? 0U : start + 1U));
}

bool lineContains(const std::string& text, const std::string& needle, const std::string& expected) {
    return contains(lineContaining(text, needle), expected);
}

int testSnapshotContract() {
    const deckflaxia::app::HybridUiShellModel shell;
    const auto snapshot = shell.buildSnapshot(deckflaxia::app::createUiSmokeInput(false));
    if (expect(snapshot.decks.size() == 4U, "JUCE UI snapshot bridge should expose four decks") != 0) {
        return 1;
    }
    if (expect(snapshot.browser.componentName == "browser-library-panel", "browser snapshot should be present") != 0) {
        return 1;
    }
    if (expect(snapshot.pluginChain.slots.size() == 20U, "plugin chain snapshot should expose four slots per deck plus master") != 0) {
        return 1;
    }
    if (expect(snapshot.midiLearn.mappingCount > 0U, "MIDI learn snapshot should bind registry state") != 0) {
        return 1;
    }
    if (expect(snapshot.status.ok, "status snapshot should be healthy") != 0) {
        return 1;
    }
    std::cout << "JuceUi.SnapshotContract decks=" << snapshot.decks.size()
              << " plugin-slots=" << snapshot.pluginChain.slots.size() << '\n';
    return 0;
}

int testUnavailableSurface() {
    std::ostringstream output;
    const auto result = deckflaxia::ui::runUnavailableJuceUiSmoke(output, true, {});
    const auto text = output.str();
    if (expect(result == 0, "unavailable dump surface should document blocker without failing fallback CTest") != 0) {
        return 1;
    }
    if (expect(contains(text, "juce-ui-smoke-test: blocked"), "unavailable surface should report blocked") != 0) {
        return 1;
    }
    if (expect(contains(text, "dump-components: blocked"), "unavailable dump should not fake a tree") != 0) {
        return 1;
    }
    if (expect(contains(text, "fallback-control-inventory: visible-native-controls=0 clickable-native-controls=0 state=no-JUCE"),
               "unavailable fallback should not claim clickable native controls") != 0) {
        return 1;
    }

    std::ostringstream screenshotOutput;
    const auto screenshotResult = deckflaxia::ui::runUnavailableJuceUiSmoke(screenshotOutput, false, ".omo/evidence/real-playable-juce/task-6-ui.png");
    if (expect(screenshotResult != 0, "unavailable screenshot should fail rather than fake PNG success") != 0) {
        return 1;
    }
    if (expect(contains(screenshotOutput.str(), "screenshot: blocked"), "unavailable screenshot should document blocker") != 0) {
        return 1;
    }
    std::cout << "JuceUi.UnavailableSurface documented=1\n";
    return 0;
}

int testComponentInventoryReport() {
    std::ostringstream output;
#if DECKFLAXIA_HAS_JUCE
    deckflaxia::ui::writeHeadlessComponentTreeReport(output);
#else
    deckflaxia::ui::runUnavailableJuceUiSmoke(output, true, {});
#endif
    const auto text = output.str();

#if DECKFLAXIA_HAS_JUCE
    if (expect(contains(text, "native-control-inventory: visible-controls classified"), "native report should include classified inventory") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: MixerCrossfaderCommandSlider family=MixerCrossfaderCommandSlider classification=wired"),
               "mixer crossfader should be classified as wired") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: Deck1PlayCommandButton family=DeckNPlayCommandButton classification=disabled state=no-deck-loaded clickable=false label=LoadDeckFirst wiring=adapter-callback"),
                "deck play button should be wired but disabled while unloaded") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: Deck4EqHighCommandSlider family=DeckNEqHighCommandSlider classification=wired state=mixer-backend clickable=true"),
                "last deck EQ slider should be classified as wired") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: ImportFilesButton family=ImportFilesButton classification=wired state=native-file-chooser-supported clickable=true label=ImportFiles"),
                 "import files control should be classified") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: ImportFolderButton family=ImportFolderButton classification=wired state=native-folder-chooser-supported clickable=true label=ImportFolder"),
                 "import folder control should be classified") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: BrowserTargetDeckSelector family=BrowserTargetDeckSelector classification=wired state=selected-deck-source clickable=true"),
                 "browser target deck selector should be classified") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: BrowserTrackTableModel family=BrowserTrackTableModel classification=wired state=empty-library clickable=true"),
                 "browser table should be classified as adapter-backed") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-deck-1-1BypassCommandButton family=PluginSlotBypassCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback"),
                "plugin slot bypass control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-deck-1-1RemoveCommandButton family=PluginSlotRemoveCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback"),
                "plugin slot remove control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-deck-1-1MoveUpCommandButton family=PluginSlotMoveUpCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback"),
                "plugin slot move-up control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-deck-1-1MoveDownCommandButton family=PluginSlotMoveDownCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin wiring=adapter-callback"),
                "plugin slot move-down control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-deck-1-1OpenEditorCommandButton family=PluginSlotOpenEditorCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin native-editor=unavailable wiring=adapter-callback"),
                "plugin slot open editor control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-master-4CloseEditorCommandButton family=PluginSlotCloseEditorCommandButton classification=disabled state=no-plugin clickable=false label=NoPlugin native-editor=unavailable wiring=adapter-callback"),
                "master plugin close editor control should be classified independently") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: plugin-slot-master-4GenericGainParameterCommandSlider family=PluginSlotParameterCommandSlider classification=disabled state=no-plugin clickable=false read-only=true wiring=adapter-callback"),
                "master plugin parameter control should be classified as read-only/disabled") != 0) {
        return 1;
    }
    if (expect(!contains(text, "BypassRemoveReorderEditorButton") && !contains(text, "PluginSlotCombinedActionButton"),
               "old combined plugin slot action surface should not remain in inventory") != 0) {
        return 1;
    }
    if (expect(contains(text, "control-inventory: MidiLearnComponent family=MidiStatusPanel classification=read-only"), "MIDI panel should be classified") != 0) {
        return 1;
    }
#else
    if (expect(contains(text, "native-control-inventory: unavailable classification=out-of-scope reason=DECKFLAXIA_HAS_JUCE=0"),
               "fallback report should classify native inventory as unavailable") != 0) {
        return 1;
    }
    if (expect(contains(text, "clickable-native-controls=0"), "fallback report should not claim clickable controls") != 0) {
        return 1;
    }
#endif

    std::cout << "JuceUi.ComponentInventory classified=1\n";
    return 0;
}

#if DECKFLAXIA_HAS_JUCE
juce::Component* findComponentById(juce::Component& component, const juce::String& componentId) {
    if (component.getComponentID() == componentId) {
        return &component;
    }
    for (int index = 0; index < component.getNumChildComponents(); ++index) {
        if (auto* child = component.getChildComponent(index)) {
            if (auto* found = findComponentById(*child, componentId)) {
                return found;
            }
        }
    }
    return nullptr;
}

bool hasUsableBounds(const juce::Component& component) {
    return component.getWidth() > 0 && component.getHeight() > 0;
}

bool rootHitTestsToControl(juce::Component& root, juce::Component& control) {
    if (!control.isEnabled() || !hasUsableBounds(control)) {
        return false;
    }
    const auto rootPoint = root.getLocalPoint(&control, control.getLocalBounds().getCentre());
    auto* hit = root.getComponentAt(rootPoint);
    return hit == &control || (hit != nullptr && control.isParentOf(hit));
}

bool isDescendantOf(const juce::Component& component, const juce::Component& ancestor) {
    for (auto* parent = component.getParentComponent(); parent != nullptr; parent = parent->getParentComponent()) {
        if (parent == &ancestor) {
            return true;
        }
    }
    return false;
}

int expectEnabledHitTestable(juce::Component& root, juce::Component* control, const std::string& message) {
    if (expect(control != nullptr, message + " should exist") != 0) {
        return 1;
    }
    if (expect(control->isEnabled(), message + " should be enabled") != 0) {
        return 1;
    }
    if (expect(hasUsableBounds(*control), message + " should have non-empty bounds") != 0) {
        return 1;
    }
    if (expect(rootHitTestsToControl(root, *control), message + " should be reachable through root hit-testing") != 0) {
        return 1;
    }
    return 0;
}

int expectDisabledNonActionable(juce::Component* control, const std::string& message) {
    if (expect(control != nullptr, message + " should exist") != 0) {
        return 1;
    }
    if (expect(!control->isEnabled(), message + " should be disabled") != 0) {
        return 1;
    }
    if (expect(!control->getComponentID().isEmpty(), message + " should keep a stable component ID") != 0) {
        return 1;
    }
    return 0;
}

int expectVisibleDeckLabel(juce::Component* label, const std::string& expectedText, const std::string& message) {
    auto* deckLabel = dynamic_cast<juce::Label*>(label);
    if (expect(deckLabel != nullptr, message + " should be a reachable JUCE label") != 0) {
        return 1;
    }
    if (expect(!deckLabel->getComponentID().isEmpty(), message + " should keep a stable component ID") != 0) {
        return 1;
    }
    if (expect(hasUsableBounds(*deckLabel), message + " should have non-empty bounds") != 0) {
        return 1;
    }
    if (expect(deckLabel->getText().toStdString() == expectedText, message + " should render data-backed text") != 0) {
        return 1;
    }
    return 0;
}


int expectStatusText(juce::Component& root, const std::string& expectedText, const std::string& message) {
    auto* status = dynamic_cast<juce::Label*>(findComponentById(root, "StatusTextLabel"));
    if (expect(status != nullptr, message + " should expose the status label") != 0) {
        return 1;
    }
    if (expect(!status->getComponentID().isEmpty(), message + " should keep a stable status label ID") != 0) {
        return 1;
    }
    if (expect(hasUsableBounds(*status), message + " should have visible status bounds") != 0) {
        return 1;
    }
    if (expect(status->getText().toStdString() == expectedText, message + " should render expected status text") != 0) {
        return 1;
    }
    return 0;
}

int testDeckPanelStateContent() {
    deckflaxia::ui::MainComponent component(true);
    component.resized();
    const auto& snapshot = component.snapshot();
    auto* deckOneState = findComponentById(component, "Deck1StateLabel");
    auto* deckOneAccent = findComponentById(component, "Deck1AccentLabel");
    auto* deckOneWaveform = findComponentById(component, "Deck1WaveformLabel");
    auto* deckOneMeter = findComponentById(component, "Deck1MeterLabel");
    auto* deckFourState = findComponentById(component, "Deck4StateLabel");
    auto* deckFourAccent = findComponentById(component, "Deck4AccentLabel");
    auto* deckFourWaveform = findComponentById(component, "Deck4WaveformLabel");
    auto* deckFourMeter = findComponentById(component, "Deck4MeterLabel");
    if (expectVisibleDeckLabel(deckOneState, "No track loaded", "deck 1 unloaded state") != 0 ||
        expectVisibleDeckLabel(deckOneAccent, "Accent: " + snapshot.decks[0].accentName, "deck 1 accent") != 0 ||
        expectVisibleDeckLabel(deckOneWaveform, "Waveform: " + snapshot.decks[0].waveform.statusText, "deck 1 waveform") != 0 ||
        expectVisibleDeckLabel(deckOneMeter, "Meter: " + snapshot.decks[0].meter.statusText, "deck 1 meter") != 0 ||
        expectVisibleDeckLabel(deckFourState, "No track loaded", "deck 4 unloaded state") != 0 ||
        expectVisibleDeckLabel(deckFourAccent, "Accent: " + snapshot.decks[3].accentName, "deck 4 accent") != 0 ||
        expectVisibleDeckLabel(deckFourWaveform, "Waveform: " + snapshot.decks[3].waveform.statusText, "deck 4 waveform") != 0 ||
        expectVisibleDeckLabel(deckFourMeter, "Meter: " + snapshot.decks[3].meter.statusText, "deck 4 meter") != 0) {
        return 1;
    }

    std::ostringstream output;
    deckflaxia::ui::writeComponentTreeReport(component, output);
    const auto text = output.str();
    if (expect(lineContains(text, "DeckComponent[1]", "children=4") && lineContains(text, "DeckComponent[4]", "children=4"),
               "deck components should not regress to title-only childless panels") != 0) {
        return 1;
    }
    if (expect(lineContains(text, "Deck1StateLabel", "bounds=") && !lineContains(text, "Deck1StateLabel", "bounds=0 0 0 0") &&
                   lineContains(text, "Deck4MeterLabel", "bounds=") && !lineContains(text, "Deck4MeterLabel", "bounds=0 0 0 0"),
               "component dump should expose non-empty deck state child bounds") != 0) {
        return 1;
    }
    std::cout << "JuceUi.DeckPanelStateContent labels=16 unloaded=NoTrackLoaded\n";
    return 0;
}

int testDeckMixerControlCallbacks() {
    deckflaxia::ui::MainComponent component(true);
    component.resized();
    auto* crossfader = dynamic_cast<juce::Slider*>(findComponentById(component, "MixerCrossfaderCommandSlider"));
    auto* deckTwoVolume = dynamic_cast<juce::Slider*>(findComponentById(component, "Deck2VolumeCommandSlider"));
    auto* deckThreeGain = dynamic_cast<juce::Slider*>(findComponentById(component, "Deck3GainCommandSlider"));
    auto* deckFourEqHigh = dynamic_cast<juce::Slider*>(findComponentById(component, "Deck4EqHighCommandSlider"));
    auto* deckOnePlay = dynamic_cast<juce::Button*>(findComponentById(component, "Deck1PlayCommandButton"));
    if (expect(crossfader != nullptr && deckTwoVolume != nullptr && deckThreeGain != nullptr && deckFourEqHigh != nullptr && deckOnePlay != nullptr,
               "deck and mixer controls should be reachable by stable component IDs") != 0) {
        return 1;
    }
    if (expectEnabledHitTestable(component, crossfader, "mixer crossfader") != 0 ||
        expectEnabledHitTestable(component, deckTwoVolume, "deck 2 volume") != 0 ||
        expectEnabledHitTestable(component, deckThreeGain, "deck 3 gain") != 0 ||
        expectEnabledHitTestable(component, deckFourEqHigh, "deck 4 EQ high") != 0) {
        return 1;
    }
    if (expectDisabledNonActionable(deckOnePlay, "unloaded deck transport") != 0) {
        return 1;
    }
    if (expectStatusText(component, component.snapshot().status.statusText, "startup AppStatusViewModel") != 0) {
        return 1;
    }
    if (expect(deckOnePlay->getButtonText() == "Load Deck", "unloaded deck transport should be labelled as unavailable") != 0) {
        return 1;
    }

    crossfader->setValue(0.75, juce::sendNotificationSync);
    if (expectStatusText(component, "status-ok: mixer crossfader mixer-command-applied", "mixer adapter success") != 0) {
        return 1;
    }
    deckTwoVolume->setValue(0.25, juce::sendNotificationSync);
    deckThreeGain->setValue(0.5, juce::sendNotificationSync);
    deckFourEqHigh->setValue(0.25, juce::sendNotificationSync);
    const auto& mixer = component.mixerSnapshot();
    if (expect(std::abs(mixer.crossfader - 0.75F) < 0.000001F, "crossfader callback should dispatch exact value through adapter") != 0) {
        return 1;
    }
    if (expect(std::abs(mixer.decks[1].volume - 0.25F) < 0.000001F, "deck 2 volume callback should dispatch exact deck/value") != 0) {
        return 1;
    }
    if (expect(std::abs(mixer.decks[2].gain - 1.0F) < 0.000001F, "deck 3 gain callback should dispatch exact deck/value to mixer scaling") != 0) {
        return 1;
    }
    if (expect(std::abs(mixer.decks[3].eqHigh - 0.5F) < 0.000001F, "deck 4 EQ high callback should dispatch exact deck/value to mixer scaling") != 0) {
        return 1;
    }

    const auto transport = deckflaxia::ui::JuceUiCommandAdapter({const_cast<deckflaxia::decks::FourDeckPlaybackCore*>(&component.playbackCore())})
                               .dispatch(deckflaxia::ui::DeckTransportIntent{deckflaxia::ui::DeckTransportAction::Play, 0});
    if (expect(transport.status == deckflaxia::ui::JuceUiCommandStatus::Unavailable &&
                   transport.domain == deckflaxia::ui::JuceUiCommandDomain::DeckTransport &&
                   transport.action == "play" && transport.detail == "deck-not-loaded",
               "unloaded deck adapter command should return exact unavailable result") != 0) {
        return 1;
    }
    std::cout << "JuceUi.DeckMixerControls wired=1 crossfader=" << mixer.crossfader << '\n';
    return 0;
}

int testBrowserControlCallbacks() {
    deckflaxia::ui::MainComponent component(true);
    component.resized();
    auto* importFiles = dynamic_cast<juce::Button*>(findComponentById(component, "ImportFilesButton"));
    auto* importFolder = dynamic_cast<juce::Button*>(findComponentById(component, "ImportFolderButton"));
    auto* targetDeck = dynamic_cast<juce::ComboBox*>(findComponentById(component, "BrowserTargetDeckSelector"));
    auto* loadSelected = dynamic_cast<juce::Button*>(findComponentById(component, "LoadSelectedBrowserTrackButton"));
    auto* table = dynamic_cast<juce::TableListBox*>(findComponentById(component, "BrowserTrackTableModel"));
    auto* deckThreePlay = dynamic_cast<juce::Button*>(findComponentById(component, "Deck3PlayCommandButton"));
    if (expect(importFiles != nullptr && importFolder != nullptr && targetDeck != nullptr && loadSelected != nullptr && table != nullptr && deckThreePlay != nullptr,
                "browser controls should be reachable by stable component IDs") != 0) {
        return 1;
    }
    if (expectEnabledHitTestable(component, importFiles, "smoke import files button") != 0 ||
        expectEnabledHitTestable(component, importFolder, "smoke import folder button") != 0 ||
        expectEnabledHitTestable(component, targetDeck, "browser target deck selector") != 0 ||
        expectEnabledHitTestable(component, table, "browser track table") != 0 ||
        expectDisabledNonActionable(loadSelected, "load selected without imported selection") != 0) {
        return 1;
    }
    if (expect(importFiles->getButtonText() == "Smoke File" && importFolder->getButtonText() == "Smoke Folder",
               "smoke browser import buttons should keep deterministic smoke labels") != 0) {
        return 1;
    }

    deckflaxia::ui::MainComponent normalComponent(false);
    normalComponent.resized();
    auto* normalImportFiles = dynamic_cast<juce::Button*>(findComponentById(normalComponent, "ImportFilesButton"));
    auto* normalImportFolder = dynamic_cast<juce::Button*>(findComponentById(normalComponent, "ImportFolderButton"));
    if (expectEnabledHitTestable(normalComponent, normalImportFiles, "normal import files native chooser") != 0 ||
        expectEnabledHitTestable(normalComponent, normalImportFolder, "normal import folder native chooser") != 0) {
        return 1;
    }
    if (expect(normalImportFiles->getButtonText() == "Import Files" && normalImportFolder->getButtonText() == "Import Folder",
               "normal browser import buttons should expose native import actions") != 0) {
        return 1;
    }

    targetDeck->setSelectedId(3, juce::dontSendNotification);
    importFiles->onClick();
    if (expect(component.browserRows().size() == 1U && component.browserRows()[0].importable() &&
                   component.browserRows()[0].entry.path.find("track_120bpm.wav") != std::string::npos,
               "supported smoke fixture import should append an importable adapter row") != 0) {
        return 1;
    }
    const auto selectResult = deckflaxia::ui::JuceUiCommandAdapter({nullptr, nullptr, nullptr, nullptr, nullptr, const_cast<std::vector<deckflaxia::library::AudioImportClassification>*>(&component.browserRows())})
                                  .dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::SelectRow, {}, 2, 0});
    if (expect(selectResult.status == deckflaxia::ui::JuceUiCommandStatus::Succeeded && selectResult.domain == deckflaxia::ui::JuceUiCommandDomain::Browser &&
                   selectResult.action == "select-row" && selectResult.detail == "browser-row-selected",
               "browser select command should return exact selected-row result") != 0) {
        return 1;
    }
    if (expect(loadSelected->isEnabled(), "supported selected browser row should enable load") != 0) {
        return 1;
    }
    if (expectEnabledHitTestable(component, loadSelected, "load selected after importable selection") != 0) {
        return 1;
    }
    loadSelected->onClick();
    const auto deckOne = deckflaxia::core::DeckId::fromIndex(0).value;
    const auto deckThree = deckflaxia::core::DeckId::fromIndex(2).value;
    if (expect(!component.playbackCore().deck(deckOne).state().loaded && component.playbackCore().deck(deckThree).state().loaded,
                "load selected should target selected deck instead of a stale/global deck") != 0) {
        return 1;
    }
    const auto directLoad = deckflaxia::ui::JuceUiCommandAdapter({const_cast<deckflaxia::decks::FourDeckPlaybackCore*>(&component.playbackCore()), nullptr, nullptr, nullptr, nullptr, nullptr})
                                .dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::LoadToDeck, component.browserRows()[0].entry, 2, 0});
    if (expect(directLoad.status == deckflaxia::ui::JuceUiCommandStatus::Succeeded && directLoad.action == "load-to-deck" &&
                   directLoad.detail == "browser-track-loaded-to-deck",
               "browser load command should report exact adapter result") != 0) {
        return 1;
    }
    if (expect(deckThreePlay->isEnabled(), "loading a deck should refresh transport controls from authoritative deck state") != 0) {
        return 1;
    }
    if (expectEnabledHitTestable(component, deckThreePlay, "loaded deck transport play") != 0) {
        return 1;
    }
    if (expect(deckThreePlay->getButtonText() == "Play", "loaded deck transport should restore action label") != 0) {
        return 1;
    }

    importFolder->onClick();
    if (expectStatusText(component, "status-error: browser import not-regular-file", "browser adapter unavailable failure") != 0) {
        return 1;
    }
    if (expect(component.browserRows().size() == 2U && !component.browserRows()[1].importable() &&
                   component.browserRows()[1].error == deckflaxia::library::AudioImportError::NotRegularFile,
               "unsupported folder smoke import should append explicit unavailable row") != 0) {
        return 1;
    }
    if (expect(!loadSelected->isEnabled(), "unsupported selected browser row should disable load") != 0) {
        return 1;
    }
    const auto invalidRow = deckflaxia::ui::JuceUiCommandAdapter({nullptr, nullptr, nullptr, nullptr, nullptr, const_cast<std::vector<deckflaxia::library::AudioImportClassification>*>(&component.browserRows())})
                                .dispatch(deckflaxia::ui::BrowserIntent{deckflaxia::ui::BrowserAction::SelectRow, {}, 2, 99});
    if (expect(invalidRow.status == deckflaxia::ui::JuceUiCommandStatus::InvalidArgument && invalidRow.action == "select-row" &&
                   invalidRow.detail == "invalid-browser-row",
               "invalid browser row should return exact adapter failure result") != 0) {
        return 1;
    }
    std::cout << "JuceUi.BrowserControls rows=" << component.browserRows().size() << " selected-deck=3\n";
    return 0;
}

int testPluginChainControls() {
    deckflaxia::ui::MainComponent component(true);
    component.resized();
    auto* viewport = findComponentById(component, "PluginChainViewport");
    auto* content = findComponentById(component, "PluginChainScrollableContent");
    auto* deckOneLabel = dynamic_cast<juce::Label*>(findComponentById(component, "plugin-slot-deck-1-1DisplayLabel"));
    auto* deckOneStatus = dynamic_cast<juce::Label*>(findComponentById(component, "plugin-slot-deck-1-1StatusLabel"));
    auto* masterFourLabel = dynamic_cast<juce::Label*>(findComponentById(component, "plugin-slot-master-4DisplayLabel"));
    auto* masterFourStatus = dynamic_cast<juce::Label*>(findComponentById(component, "plugin-slot-master-4StatusLabel"));
    auto* bypass = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1BypassCommandButton"));
    auto* remove = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1RemoveCommandButton"));
    auto* moveUp = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1MoveUpCommandButton"));
    auto* moveDown = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1MoveDownCommandButton"));
    auto* openEditor = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1OpenEditorCommandButton"));
    auto* closeEditor = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-1CloseEditorCommandButton"));
    auto* parameter = dynamic_cast<juce::Slider*>(findComponentById(component, "plugin-slot-deck-1-1GenericGainParameterCommandSlider"));
    auto* deckTwoBypass = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-deck-1-2BypassCommandButton"));
    auto* masterFourCloseEditor = dynamic_cast<juce::Button*>(findComponentById(component, "plugin-slot-master-4CloseEditorCommandButton"));
    auto* masterFourParameter = dynamic_cast<juce::Slider*>(findComponentById(component, "plugin-slot-master-4GenericGainParameterCommandSlider"));
    if (expect(viewport != nullptr && content != nullptr && deckOneLabel != nullptr && deckOneStatus != nullptr && masterFourLabel != nullptr && masterFourStatus != nullptr,
               "plugin chain should expose named viewport/content and stable row labels") != 0) {
        return 1;
    }
    if (expect(bypass != nullptr && remove != nullptr && moveUp != nullptr && moveDown != nullptr && openEditor != nullptr && closeEditor != nullptr && parameter != nullptr &&
                   deckTwoBypass != nullptr && masterFourCloseEditor != nullptr && masterFourParameter != nullptr,
               "plugin slot controls should be split into stable reachable component IDs") != 0) {
        return 1;
    }
    if (expect(isDescendantOf(*bypass, *content) && isDescendantOf(*masterFourParameter, *content),
               "plugin row controls should live inside PluginChainScrollableContent") != 0) {
        return 1;
    }
    if (expect(hasUsableBounds(*viewport) && hasUsableBounds(*content) && content->getHeight() > viewport->getHeight(),
               "plugin chain content should be scrollable inside the named viewport") != 0) {
        return 1;
    }
    if (expect(hasUsableBounds(*deckOneLabel) && hasUsableBounds(*bypass) && hasUsableBounds(*masterFourLabel) && hasUsableBounds(*masterFourParameter),
               "row 1 and row 20 should keep non-empty reachable bounds inside scrollable content") != 0) {
        return 1;
    }
    if (expect(deckOneLabel->getText() == "Deck 1 Slot 1" && deckOneStatus->getText().contains("empty plugin placeholder") &&
                   masterFourLabel->getText() == "Master Slot 4" && masterFourStatus->getText().contains("empty master plugin placeholder"),
               "plugin rows should surface data-backed labels/status without faking plugin availability") != 0) {
        return 1;
    }
    if (expect(!bypass->getBounds().intersects(deckTwoBypass->getBounds()),
               "representative visible plugin rows should not overlap inside scrollable content") != 0) {
        return 1;
    }
    if (expectDisabledNonActionable(bypass, "empty plugin bypass placeholder") != 0 ||
        expectDisabledNonActionable(remove, "empty plugin remove placeholder") != 0 ||
        expectDisabledNonActionable(moveUp, "empty plugin move-up placeholder") != 0 ||
        expectDisabledNonActionable(moveDown, "empty plugin move-down placeholder") != 0 ||
        expectDisabledNonActionable(openEditor, "empty plugin open-editor placeholder") != 0 ||
        expectDisabledNonActionable(closeEditor, "empty plugin close-editor placeholder") != 0 ||
        expectDisabledNonActionable(parameter, "empty plugin parameter placeholder") != 0 ||
        expectDisabledNonActionable(masterFourCloseEditor, "empty master plugin close-editor placeholder") != 0 ||
        expectDisabledNonActionable(masterFourParameter, "empty master plugin parameter placeholder") != 0) {
        return 1;
    }
    if (expect(bypass->getButtonText() == "No Plugin" && remove->getButtonText() == "No Plugin" && openEditor->getButtonText() == "No Plugin" &&
                   masterFourCloseEditor->getButtonText() == "No Plugin",
               "empty plugin placeholder buttons should not display action labels") != 0) {
        return 1;
    }
    const auto unavailable = deckflaxia::ui::JuceUiCommandAdapter({}).dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Bypass,
                                                                                                                 deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                                                                 0,
                                                                                                                 0,
                                                                                                                 true});
    if (expect(unavailable.status == deckflaxia::ui::JuceUiCommandStatus::Unavailable &&
                   unavailable.domain == deckflaxia::ui::JuceUiCommandDomain::PluginChain && unavailable.action == "bypass" &&
                   unavailable.detail == "plugin-chain-backend-unavailable" && !unavailable.ok(),
               "empty plugin chain adapter command should not report success") != 0) {
        return 1;
    }
    deckflaxia::core::PluginChainDescriptor emptyDescriptor{"empty", {}};
    const auto invalidSlot = deckflaxia::ui::JuceUiCommandAdapter({nullptr, nullptr, nullptr, nullptr, &emptyDescriptor})
                                 .dispatch(deckflaxia::ui::PluginChainIntent{deckflaxia::ui::PluginChainAction::Parameter,
                                                                              deckflaxia::plugins::PluginChainTargetKind::Deck,
                                                                              0,
                                                                              0,
                                                                              false,
                                                                              "gain",
                                                                              0.25});
    if (expect(invalidSlot.status == deckflaxia::ui::JuceUiCommandStatus::InvalidArgument && invalidSlot.action == "parameter" &&
                   invalidSlot.detail == "invalid-plugin-slot-or-parameter",
               "empty plugin descriptor parameter should return exact invalid slot result") != 0) {
        return 1;
    }
    std::cout << "JuceUi.PluginControls split=1 scrollable=1 empty-disabled=1\n";
    return 0;
}

int testRefreshHonestyRules() {
    deckflaxia::ui::MainComponent component(true);
    component.resized();
    auto* crossfader = dynamic_cast<juce::Slider*>(findComponentById(component, "MixerCrossfaderCommandSlider"));
    auto* deckTwoVolume = dynamic_cast<juce::Slider*>(findComponentById(component, "Deck2VolumeCommandSlider"));
    auto* deckOnePlay = dynamic_cast<juce::Button*>(findComponentById(component, "Deck1PlayCommandButton"));
    auto* loadSelected = dynamic_cast<juce::Button*>(findComponentById(component, "LoadSelectedBrowserTrackButton"));
    auto* pluginParameter = dynamic_cast<juce::Slider*>(findComponentById(component, "plugin-slot-deck-1-1GenericGainParameterCommandSlider"));
    if (expect(crossfader != nullptr && deckTwoVolume != nullptr && deckOnePlay != nullptr && loadSelected != nullptr && pluginParameter != nullptr,
               "refresh test controls should be reachable by stable component IDs") != 0) {
        return 1;
    }

    crossfader->setValue(0.72, juce::sendNotificationSync);
    deckTwoVolume->setValue(0.31, juce::sendNotificationSync);
    crossfader->setValue(0.05, juce::dontSendNotification);
    deckTwoVolume->setValue(0.05, juce::dontSendNotification);
    if (expect(std::abs(component.mixerSnapshot().crossfader - 0.72F) < 0.000001F &&
                   std::abs(component.mixerSnapshot().decks[1].volume - 0.31F) < 0.000001F,
               "dontSendNotification staging should not overwrite authoritative mixer state before refresh") != 0) {
        return 1;
    }
    component.refreshFromAuthoritativeState();
    const auto& mixer = component.mixerSnapshot();
    if (expect(std::abs(mixer.crossfader - 0.72F) < 0.000001F && std::abs(crossfader->getValue() - 0.72) < 0.000001,
               "refresh should hydrate crossfader from authoritative mixer snapshot without stale UI overwrite") != 0) {
        return 1;
    }
    if (expect(std::abs(mixer.decks[1].volume - 0.31F) < 0.000001F && std::abs(deckTwoVolume->getValue() - 0.31) < 0.000001,
               "refresh should hydrate deck mixer slider from authoritative mixer snapshot") != 0) {
        return 1;
    }
    if (expect(!deckOnePlay->isEnabled() && !loadSelected->isEnabled() && !pluginParameter->isEnabled(),
               "no-deck, empty-library, and no-plugin placeholders should remain disabled after refresh") != 0) {
        return 1;
    }

    std::ostringstream output;
    deckflaxia::ui::writeComponentTreeReport(component, output);
    const auto text = output.str();
    if (expect(lineContains(text, "Deck1PlayCommandButton", "enabled=false") && lineContains(text, "LoadSelectedBrowserTrackButton", "enabled=false"),
               "component dump should expose disabled placeholder state after refresh") != 0) {
        return 1;
    }
    if (expect(lineContains(text, "MixerCrossfaderCommandSlider", "enabled=true") &&
                   lineContains(text, "MixerCrossfaderCommandSlider", "bounds=") &&
                   lineContains(text, "MixerCrossfaderCommandSlider", "clickable=true") &&
                   lineContains(text, "Deck1PlayCommandButton", "clickable=false"),
               "component dump should expose enabled/bounds/clickable metadata") != 0) {
        return 1;
    }
    if (expect(contains(text, "native-control-inventory: live-runtime component-dump-authoritative") &&
                   !contains(text, "control-inventory: ImportFilesButton") &&
                   lineContains(text, "ImportFilesButton", "enabled=true") &&
                   lineContains(text, "ImportFilesButton", "clickable=true"),
               "live component report should not contradict runtime import button clickability") != 0) {
        return 1;
    }
    std::cout << "JuceUi.RefreshHonesty hydrated=1 disabled-placeholders=1\n";
    return 0;
}
#endif

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    if (filter == "snapshot") {
        return testSnapshotContract();
    }
    if (filter == "unavailable") {
        return testUnavailableSurface();
    }
    if (filter == "inventory") {
        return testComponentInventoryReport();
    }
    if (filter == "deck-mixer") {
#if DECKFLAXIA_HAS_JUCE
        return testDeckMixerControlCallbacks();
#else
        std::cout << "JuceUi.DeckMixerControls blocked reason=DECKFLAXIA_HAS_JUCE=0\n";
        return 0;
#endif
    }
    if (filter == "deck-panels") {
#if DECKFLAXIA_HAS_JUCE
        return testDeckPanelStateContent();
#else
        std::cout << "JuceUi.DeckPanelStateContent blocked reason=DECKFLAXIA_HAS_JUCE=0\n";
        return 0;
#endif
    }
    if (filter == "browser") {
#if DECKFLAXIA_HAS_JUCE
        return testBrowserControlCallbacks();
#else
        std::cout << "JuceUi.BrowserControls blocked reason=DECKFLAXIA_HAS_JUCE=0\n";
        return 0;
#endif
    }
    if (filter == "plugin") {
#if DECKFLAXIA_HAS_JUCE
        return testPluginChainControls();
#else
        std::cout << "JuceUi.PluginControls blocked reason=DECKFLAXIA_HAS_JUCE=0\n";
        return 0;
#endif
    }
    if (filter == "refresh") {
#if DECKFLAXIA_HAS_JUCE
        return testRefreshHonestyRules();
#else
        std::cout << "JuceUi.RefreshHonesty blocked reason=DECKFLAXIA_HAS_JUCE=0\n";
        return 0;
#endif
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown JuceUi filter " << filter << '\n';
        return 1;
    }
    if (testSnapshotContract() != 0 || testUnavailableSurface() != 0 || testComponentInventoryReport() != 0) {
        return 1;
    }
#if DECKFLAXIA_HAS_JUCE
    if (testDeckPanelStateContent() != 0 || testDeckMixerControlCallbacks() != 0 || testBrowserControlCallbacks() != 0 || testPluginChainControls() != 0 || testRefreshHonestyRules() != 0) {
        return 1;
    }
#endif
    std::cout << "Juce UI smoke tests passed\n";
    return 0;
}
