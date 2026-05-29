#include "ui/VST3EditorUi.h"

#include "decks/FourDeckPlaybackCore.h"
#include "plugins/PluginChainProcessor.h"

#include <cmath>
#include <filesystem>
#include <iostream>
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

float renderWithPluginEditorRoundTrip(bool bypassed) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    auto media = deckflaxia::decks::PreparedAudioMedia::deterministicTestWav(4096, 48000);
    if (!core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::deterministicTestWav(std::move(media))).ok()) {
        return 0.0F;
    }
    deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeDeterministicGainPlugin(0.25, bypassed)}};
    if (!core.setDeckPluginChain(deckId, chain).ok()) {
        return 0.0F;
    }
    const auto open = core.deckPluginChain(deckId).openSeparateEditorWindow(0);
    const auto close = core.deckPluginChain(deckId).closeSeparateEditorWindow(0);
    if (!open.genericParameterSurfaceAvailable || close.open) {
        return 0.0F;
    }
    if (core.play(deckId) != deckflaxia::decks::FourDeckPlaybackError::None) {
        return 0.0F;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 1);
    if (!render.ok()) {
        return 0.0F;
    }
    const auto& buffer = core.lastInterleavedOutput();
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < 512U; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * deckflaxia::decks::kFourDeckOutputChannels;
        sum += static_cast<double>(buffer[index]) * static_cast<double>(buffer[index]);
        sum += static_cast<double>(buffer[index + 1U]) * static_cast<double>(buffer[index + 1U]);
    }
    return static_cast<float>(std::sqrt(sum / 1024.0));
}

int testModelControls() {
    auto first = deckflaxia::plugins::makeDeterministicGainPlugin(0.5, false);
    auto second = deckflaxia::plugins::makeDeterministicGainPlugin(0.25, false);
    second.identifier = "deterministic:gain-b";
    second.displayName = "Deterministic Gain B";
    deckflaxia::core::PluginChainDescriptor chain{"deck-a", {first, second}};

    deckflaxia::plugins::OfflinePluginChainHost host;
    if (expect(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512).ok(), "host configure should pass") != 0) {
        return 1;
    }
    auto model = deckflaxia::ui::buildPluginChainEditorModel(deckflaxia::plugins::PluginChainTargetKind::Deck, host.chainState(), host.status());
    if (expect(model.slots.size() == 2U, "model should expose chain slots") != 0) {
        return 1;
    }
    const std::vector<std::string> expectedControls{"bypass", "remove", "move-up", "move-down", "open-editor", "close-editor"};
    if (expect(model.slots[0].controls == expectedControls && !model.slots[0].parameters.empty(), "slot should expose exact controls and parameters") != 0) {
        return 1;
    }
    if (expect(model.slots[0].componentName == "deck-plugin-slot-1" &&
                   model.slots[0].parameters[0].componentName == "deck-plugin-slot-1-parameter-gain" &&
                   model.slots[0].parameters[0].identifier == "gain",
               "plugin model should expose exact stable component and parameter names") != 0) {
        return 1;
    }
    if (expect(model.sandboxStatus.find("cross-process") != std::string::npos, "model should disclose sandbox boundary") != 0) {
        return 1;
    }
    if (expect(deckflaxia::ui::setPluginSlotBypass(chain, 0, true) && chain.plugins[0].bypassed, "bypass command should mutate chain") != 0) {
        return 1;
    }
    if (expect(deckflaxia::ui::setPluginParameter(chain, 0, "gain", 0.125), "parameter command should mutate chain") != 0) {
        return 1;
    }
    if (expect(deckflaxia::ui::movePluginSlot(chain, 1, 0) && chain.plugins[0].displayName == "Deterministic Gain B", "reorder command should move slots") != 0) {
        return 1;
    }
    if (expect(deckflaxia::ui::removePluginSlot(chain, 1) && chain.plugins.size() == 1U, "remove command should delete slot") != 0) {
        return 1;
    }
    deckflaxia::core::PluginChainDescriptor reloaded;
    if (expect(deckflaxia::ui::saveAndReloadPluginChain(chain, reloaded) && reloaded.plugins.size() == 1U, "state should save and reload") != 0) {
        return 1;
    }
    std::cout << "VST3EditorUi.ModelControls slots=" << model.slots.size() << " controls=" << model.slots[0].controls.size() << '\n';
    return 0;
}


int testEmptyPluginChain() {
    deckflaxia::core::PluginChainDescriptor empty{"empty", {}};
    deckflaxia::plugins::OfflinePluginChainHost host;
    if (expect(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, empty, 48000.0, 512).ok(), "empty host configure should pass") != 0) {
        return 1;
    }
    const auto model = deckflaxia::ui::buildPluginChainEditorModel(deckflaxia::plugins::PluginChainTargetKind::Deck, host.chainState(), host.status());
    if (expect(model.slots.empty(), "empty plugin chain model should expose no fake active slots") != 0) {
        return 1;
    }
    if (expect(!deckflaxia::ui::setPluginSlotBypass(empty, 0, true), "empty chain bypass helper should not report success") != 0) {
        return 1;
    }
    if (expect(!deckflaxia::ui::setPluginParameter(empty, 0, "gain", 0.5), "empty chain parameter helper should not report success") != 0) {
        return 1;
    }
    if (expect(!deckflaxia::ui::removePluginSlot(empty, 0), "empty chain remove helper should not report success") != 0) {
        return 1;
    }
    if (expect(!deckflaxia::ui::movePluginSlot(empty, 0, 1), "empty chain reorder helper should not report success") != 0) {
        return 1;
    }
    if (expect(empty.plugins.empty(), "empty chain helper failures should leave authoritative descriptor empty") != 0) {
        return 1;
    }
    const auto editor = host.openSeparateEditorWindow(0);
    if (expect(!editor.open && editor.statusText == "invalid-slot", "empty host editor open should be explicitly invalid") != 0) {
        return 1;
    }
    std::cout << "VST3EditorUi.EmptyPluginChain no-fake-slots=1\n";
    return 0;
}

int testNoEditorGenericFallback(const std::filesystem::path& fixtures) {
    std::ostringstream output;
    const auto result = deckflaxia::ui::runVst3EditorSmokeTest(output, deckflaxia::ui::VST3EditorSmokeOptions{fixtures, ".omo/evidence/real-playable-juce/task-11-editor.png"});
    const auto text = output.str();
    if (expect(result == 0, "editor smoke should pass through fallback surface") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("native-editor-open=0") != std::string::npos, "fallback should not claim native editor opened") != 0) {
        return 1;
    }
    if (expect(text.find("generic-parameters=1") != std::string::npos, "fallback should expose generic parameters") != 0) {
        return 1;
    }
    if (expect(text.find("real-vst3-instantiated=0") != std::string::npos, "fallback should report honest real VST3 unavailability") != 0) {
        return 1;
    }
    if (expect(text.find("screenshot: blocked") != std::string::npos, "fallback screenshot should be honestly blocked") != 0) {
        return 1;
    }
    std::cout << "VST3EditorUi.NoEditorGenericFallback documented=1\n";
    return 0;
}

int testProcessingPreserved() {
    const auto processed = renderWithPluginEditorRoundTrip(false);
    const auto bypassed = renderWithPluginEditorRoundTrip(true);
    if (expect(processed > 0.0F && bypassed > 0.0F, "renders should remain alive during editor lifecycle") != 0) {
        return 1;
    }
    if (expect(std::abs(processed - bypassed) > 0.0001F, "bypass should still affect processing independent of editor") != 0) {
        return 1;
    }
    std::cout << "VST3EditorUi.ProcessingPreserved processed-rms=" << processed << " bypass-rms=" << bypassed << '\n';
    return 0;
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    return argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures/plugins");
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);
    if (filter == "model") {
        return testModelControls();
    }
    if (filter == "no-editor") {
        return testNoEditorGenericFallback(fixtures);
    }
    if (filter == "empty") {
        return testEmptyPluginChain();
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown VST3EditorUi filter " << filter << '\n';
        return 1;
    }
    if (testModelControls() != 0 || testEmptyPluginChain() != 0 || testNoEditorGenericFallback(fixtures) != 0 || testProcessingPreserved() != 0) {
        return 1;
    }
    std::cout << "VST3 editor UI tests passed\n";
    return 0;
}
