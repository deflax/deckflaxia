#include "audio/AudioDeckSmoke.h"
#include "decks/FourDeckPlaybackCore.h"
#include "persistence/Persistence.h"
#include "plugins/PluginChainProcessor.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

template <typename T>
int expectOk(const T& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

float masterRms(const deckflaxia::decks::FourDeckPlaybackCore& core) {
    const auto& buffer = core.lastInterleavedOutput();
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < 512U; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * deckflaxia::decks::kFourDeckOutputChannels;
        sum += static_cast<double>(buffer[index]) * static_cast<double>(buffer[index]);
        sum += static_cast<double>(buffer[index + 1U]) * static_cast<double>(buffer[index + 1U]);
    }
    return static_cast<float>(std::sqrt(sum / 1024.0));
}

float renderDeckWithPlugin(bool bypassed) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    auto media = deckflaxia::decks::PreparedAudioMedia::deterministicTestWav(4096, 48000);
    if (expect(core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::deterministicTestWav(std::move(media))).ok(), "test media should load") != 0) {
        return 0.0F;
    }
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeDeterministicGainPlugin(0.25, bypassed)}};
    if (expectOk(core.setDeckPluginChain(deckId, chain), "deck plugin chain configure") != 0) {
        return 0.0F;
    }
    if (expect(core.play(deckId) == deckflaxia::decks::FourDeckPlaybackError::None, "deck should play") != 0) {
        return 0.0F;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 1);
    if (expect(render.ok(), "offline render should succeed") != 0) {
        return 0.0F;
    }
    return masterRms(core);
}

int testDeckProcessingChangesAudio() {
    const auto processed = renderDeckWithPlugin(false);
    const auto bypassed = renderDeckWithPlugin(true);
    if (expect(processed > 0.0F && bypassed > 0.0F, "processed and bypass renders should be measurable") != 0) {
        return 1;
    }
    if (expect(std::abs(processed - bypassed) > 0.0001F, "deck plugin processing should change audio against bypass") != 0) {
        return 1;
    }
    std::cout << "VST3Processing.DeckProcessingChangesAudio processed-rms=" << processed << " bypass-rms=" << bypassed << '\n';
    return 0;
}

int testParametersAndLatency() {
    deckflaxia::plugins::OfflinePluginChainHost host;
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeDeterministicGainPlugin(0.5, false)}};
    if (expectOk(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "plugin host configure") != 0) {
        return 1;
    }
    if (expectOk(host.setParameter(0, "gain", 0.125), "gain parameter set") != 0) {
        return 1;
    }
    if (expect(std::abs(host.parameter(0, "gain") - 0.125) < 0.000001, "gain parameter should round trip") != 0) {
        return 1;
    }
    const auto status = host.status();
    if (expect(status.latencyFrames == 0U && status.activeSlotCount == 1U, "deterministic plugin should report latency and active slot") != 0) {
        return 1;
    }
    std::cout << "VST3Processing.ParameterLatency backend=" << deckflaxia::plugins::toString(status.backend)
              << " latency=" << status.latencyFrames << " gain=" << host.parameter(0, "gain") << '\n';
    return 0;
}

int testMasterChainStateReload() {
    deckflaxia::persistence::PersistenceService service;
    auto plugin = deckflaxia::plugins::makeDeterministicGainPlugin(0.42, false);
    plugin.latencyFrames = 7;
    const deckflaxia::core::PluginChainDescriptor saved{"master", {plugin}};
    if (expectOk(service.pluginChains().save(saved), "master chain save") != 0) {
        return 1;
    }
    const auto loaded = service.pluginChains().load("master");
    if (expectOk(loaded, "master chain reload") != 0) {
        return 1;
    }
    const auto identical = loaded.value.plugins.size() == 1U &&
                           loaded.value.plugins[0].identifier == plugin.identifier &&
                           loaded.value.plugins[0].latencyFrames == 7U &&
                           loaded.value.plugins[0].parameters.size() == 1U &&
                           std::abs(loaded.value.plugins[0].parameters[0].normalizedValue - 0.42) < 0.000001;
    if (expect(identical, "master plugin chain state should reload identically") != 0) {
        return 1;
    }
    deckflaxia::decks::FourDeckPlaybackCore core;
    if (expectOk(core.setMasterPluginChain(loaded.value), "master chain configure after reload") != 0) {
        return 1;
    }
    std::cout << "VST3Processing.MasterChainStateReload plugins=" << loaded.value.plugins.size()
              << " value=" << loaded.value.plugins[0].parameters[0].normalizedValue << '\n';
    return 0;
}

int testSmokeSurface(const std::filesystem::path& fixtures) {
    std::ostringstream output;
    const auto result = deckflaxia::audio::runVst3ProcessingSmokeTest(output, deckflaxia::audio::AudioDeckSmokeOptions{fixtures, {}, "deck-a"});
    const auto text = output.str();
    if (expect(result == 0, "VST3 processing smoke should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    return expect(text.find("changed-audio=1") != std::string::npos && text.find("real-vst3-success=1") == std::string::npos,
                  "smoke should report changed audio without claiming fixture fallback as real VST3 success");
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    return argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures/plugins");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);
    if (filter == "deck") {
        return testDeckProcessingChangesAudio();
    }
    if (filter == "parameters") {
        return testParametersAndLatency();
    }
    if (filter == "master-state") {
        return testMasterChainStateReload();
    }
    if (filter == "smoke") {
        return testSmokeSurface(fixtures);
    }
    if (testDeckProcessingChangesAudio() != 0 || testParametersAndLatency() != 0 || testMasterChainStateReload() != 0 || testSmokeSurface(fixtures) != 0) {
        return 1;
    }
    std::cout << "VST3 processing tests passed\n";
    return 0;
}
