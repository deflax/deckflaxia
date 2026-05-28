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
#include <vector>

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

std::vector<float> deterministicStereoBuffer() {
    std::vector<float> buffer(512U * 2U);
    for (std::uint32_t frame = 0; frame < 512U; ++frame) {
        const auto left = static_cast<float>(((frame % 64U) + 1U) / 128.0);
        const auto right = static_cast<float>(((frame % 31U) + 1U) / 96.0);
        const auto index = static_cast<std::size_t>(frame) * 2U;
        buffer[index] = left;
        buffer[index + 1U] = right;
    }
    return buffer;
}

double rmsOf(const std::vector<float>& buffer) {
    double sum = 0.0;
    for (const auto sample : buffer) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return std::sqrt(sum / static_cast<double>(buffer.size()));
}

double renderHostRms(deckflaxia::plugins::OfflinePluginChainHost& host) {
    auto buffer = deterministicStereoBuffer();
    const auto metrics = host.processReplacing(buffer.data(), 512, false);
    const auto measured = rmsOf(buffer);
    if (expect(std::abs(metrics.outputRms - measured) < 0.000001, "host metrics should match rendered buffer") != 0) {
        return 0.0;
    }
    return measured;
}

int expectRealFixtureLoaded(const deckflaxia::plugins::OfflinePluginChainHost& host, const std::string& label) {
    const auto status = host.status();
    if (expect(status.backend == deckflaxia::plugins::PluginProcessingBackendKind::JuceVst3, label + " should use JUCE VST3 backend") != 0) {
        return 1;
    }
    if (expect(status.realVst3Instantiated, label + " should instantiate a real VST3") != 0) {
        if (!status.unavailableReason.empty()) {
            std::cerr << status.unavailableReason << '\n';
        }
        return 1;
    }
    return 0;
}

bool approximatelyRatio(double numerator, double denominator, double expected) {
    if (denominator <= 0.0) {
        return false;
    }
    return std::abs((numerator / denominator) - expected) < 0.05;
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

int testDeterministicFixtureIsNotRealVst3() {
    deckflaxia::plugins::OfflinePluginChainHost host;
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeDeterministicGainPlugin(0.5, false)}};
    if (expectOk(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "deterministic plugin host configure") != 0) {
        return 1;
    }
    const auto status = host.status();
    if (expect(!status.realVst3Instantiated, "deterministic fixture must not report real VST3 instantiation") != 0) {
        return 1;
    }
    std::cout << "VST3Processing.DeterministicFixtureIsNotRealVst3 backend=" << deckflaxia::plugins::toString(status.backend)
              << " real-vst3-instantiated=" << (status.realVst3Instantiated ? 1 : 0) << '\n';
    return 0;
}

int testMissingRealFixtureManifest(const std::filesystem::path& fixtures) {
    const auto manifest = deckflaxia::plugins::loadRealVst3FixtureManifest(fixtures / "missing-real-vst3-fixture-manifest.json");
    if (expect(!manifest.ok(), "missing real fixture manifest should fail") != 0) {
        return 1;
    }
    if (expect(manifest.error == deckflaxia::plugins::RealVst3FixtureManifestError::ManifestMissing, "missing real fixture manifest should report typed missing error") != 0) {
        return 1;
    }
    std::cout << "VST3Processing.MissingRealFixtureManifest error=" << deckflaxia::plugins::toString(manifest.error)
              << " reason=" << manifest.reason << '\n';
    return 0;
}

deckflaxia::plugins::RealVst3FixtureManifestResult loadRequiredRealManifest(const std::filesystem::path& fixtures) {
    const auto manifest = deckflaxia::plugins::loadRealVst3FixtureManifest(fixtures / "manifest.json");
    if (!manifest.ok() && !manifest.reason.empty()) {
        std::cerr << manifest.reason << '\n';
    }
    return manifest;
}

int testRealFixtureManifestProductionHost(const std::filesystem::path& fixtures) {
    const auto manifest = loadRequiredRealManifest(fixtures);
    if (expectOk(manifest, "real VST3 fixture manifest") != 0) {
        return 1;
    }
    deckflaxia::plugins::OfflinePluginChainHost host;
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeRealVst3FixturePlugin(manifest.manifest, false)}};
    if (expectOk(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "real VST3 plugin host configure") != 0) {
        return 1;
    }
    if (expectRealFixtureLoaded(host, "real fixture") != 0) {
        return 1;
    }
    const auto status = host.status();
    std::cout << "VST3Processing.RealFixture backend=" << deckflaxia::plugins::toString(status.backend)
              << " real-vst3-instantiated=" << (status.realVst3Instantiated ? 1 : 0) << '\n';
    return 0;
}

int testRealFixtureProcessing(const std::filesystem::path& fixtures) {
    const auto manifest = loadRequiredRealManifest(fixtures);
    if (expectOk(manifest, "real VST3 fixture manifest") != 0) {
        return 1;
    }
    deckflaxia::plugins::OfflinePluginChainHost processedHost;
    deckflaxia::plugins::OfflinePluginChainHost bypassHost;
    const deckflaxia::core::PluginChainDescriptor processedChain{"deck-a", {deckflaxia::plugins::makeRealVst3FixturePlugin(manifest.manifest, false)}};
    const deckflaxia::core::PluginChainDescriptor bypassChain{"deck-a", {deckflaxia::plugins::makeRealVst3FixturePlugin(manifest.manifest, true)}};
    if (expectOk(processedHost.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, processedChain, 48000.0, 512), "real processed host configure") != 0 ||
        expectOk(bypassHost.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, bypassChain, 48000.0, 512), "real bypass host configure") != 0 ||
        expectRealFixtureLoaded(processedHost, "real processed host") != 0 || expectRealFixtureLoaded(bypassHost, "real bypass host") != 0) {
        return 1;
    }
    const auto processed = renderHostRms(processedHost);
    const auto bypassed = renderHostRms(bypassHost);
    if (expect(processed > 0.0 && bypassed > 0.0, "real processed and bypass renders should be measurable") != 0) {
        return 1;
    }
    if (expect(approximatelyRatio(processed, bypassed, 0.5), "real default gain should render at roughly half bypass RMS") != 0) {
        std::cerr << "processed-rms=" << processed << " bypass-rms=" << bypassed << '\n';
        return 1;
    }
    std::cout << "VST3Processing.RealProcessing processed-rms=" << processed << " bypass-rms=" << bypassed
              << " ratio=" << (processed / bypassed) << '\n';
    return 0;
}

int testRealFixtureParameters(const std::filesystem::path& fixtures) {
    const auto manifest = loadRequiredRealManifest(fixtures);
    if (expectOk(manifest, "real VST3 fixture manifest") != 0) {
        return 1;
    }
    deckflaxia::plugins::OfflinePluginChainHost host;
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeRealVst3FixturePlugin(manifest.manifest, false)}};
    if (expectOk(host.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "real parameter host configure") != 0 ||
        expectRealFixtureLoaded(host, "real parameter host") != 0) {
        return 1;
    }
    const auto defaultRms = renderHostRms(host);
    if (expectOk(host.setParameter(0, "gain", 0.25), "real gain parameter set") != 0) {
        return 1;
    }
    if (expect(std::abs(host.parameter(0, "gain") - 0.25) < 0.000001, "real gain parameter should round trip through host state") != 0) {
        return 1;
    }
    const auto changedRms = renderHostRms(host);
    if (expect(approximatelyRatio(changedRms, defaultRms, 0.5), "real gain 0.25 should halve output relative to default 0.5") != 0) {
        std::cerr << "default-rms=" << defaultRms << " changed-rms=" << changedRms << '\n';
        return 1;
    }
    std::cout << "VST3Processing.RealParameters default-rms=" << defaultRms << " changed-rms=" << changedRms
              << " gain=" << host.parameter(0, "gain") << '\n';
    return 0;
}

int testRealFixtureStateRoundtrip(const std::filesystem::path& fixtures) {
    const auto manifest = loadRequiredRealManifest(fixtures);
    if (expectOk(manifest, "real VST3 fixture manifest") != 0) {
        return 1;
    }
    const deckflaxia::core::PluginChainDescriptor chain{"deck-a", {deckflaxia::plugins::makeRealVst3FixturePlugin(manifest.manifest, false)}};
    deckflaxia::plugins::OfflinePluginChainHost source;
    if (expectOk(source.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "real source host configure") != 0 ||
        expectRealFixtureLoaded(source, "real source host") != 0 ||
        expectOk(source.setParameter(0, "gain", 0.25), "real source gain parameter set") != 0) {
        return 1;
    }
    deckflaxia::plugins::PluginStateSnapshot snapshot;
    if (expectOk(source.snapshotState(0, snapshot), "real source state snapshot") != 0 || expect(!snapshot.bytes.empty(), "real source state snapshot should not be empty") != 0) {
        return 1;
    }
    const auto sourceRms = renderHostRms(source);
    deckflaxia::plugins::OfflinePluginChainHost reloaded;
    if (expectOk(reloaded.configure(deckflaxia::plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512), "real reloaded host configure") != 0 ||
        expectRealFixtureLoaded(reloaded, "real reloaded host") != 0 ||
        expectOk(reloaded.restoreState(0, snapshot), "real reloaded state restore") != 0) {
        return 1;
    }
    const auto reloadedRms = renderHostRms(reloaded);
    if (expect(std::abs(reloaded.parameter(0, "gain") - 0.25) < 0.01, "real restored gain parameter should match saved state") != 0) {
        return 1;
    }
    if (expect(std::abs(reloadedRms - sourceRms) < 0.0001, "real restored state should render same output behavior") != 0) {
        std::cerr << "source-rms=" << sourceRms << " reloaded-rms=" << reloadedRms << '\n';
        return 1;
    }
    std::cout << "VST3Processing.RealState source-rms=" << sourceRms << " reloaded-rms=" << reloadedRms
              << " gain=" << reloaded.parameter(0, "gain") << " state-bytes=" << snapshot.bytes.size() << '\n';
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
    if (result != 0) {
        if (expect(text.find("real-vst3-fixture-manifest=") != std::string::npos && text.find("error=manifest-missing") != std::string::npos,
                   "smoke should fail honestly when the expected real manifest is missing") != 0) {
            std::cerr << text;
            return 1;
        }
        std::cout << "VST3Processing.SmokeSurface honest-missing-manifest\n";
        return 0;
    }
    if (expect(text.find("changed-audio=1") != std::string::npos, "smoke should report changed audio") != 0) {
        return 1;
    }
    if (text.find("real-vst3-instantiated=1") != std::string::npos) {
        if (expect(text.find("descriptor=vst3:") != std::string::npos, "real VST3 success should come from a vst3 descriptor") != 0) {
            return 1;
        }
        if (expect(text.find("real-vst3-fixture-id=") != std::string::npos && text.find("bundle-path=") != std::string::npos,
                   "real VST3 success should identify the generated fixture") != 0) {
            return 1;
        }
        if (expect(text.find("real-parameters=1") != std::string::npos && text.find("real-state=1") != std::string::npos,
                   "real VST3 success should report parameter and state markers") != 0) {
            return 1;
        }
        return 0;
    }
    if (expect(text.find("real-vst3-success=1") == std::string::npos,
               "smoke should never claim real VST3 success from fallback fixtures") != 0) {
        return 1;
    }
    return expect(text.find("real-vst3-instantiated=0") != std::string::npos || text.find("juce-vst3-host=unavailable") != std::string::npos,
                  "smoke should report honest fallback or unavailable status");
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
    if (filter == "deterministic-not-real") {
        return testDeterministicFixtureIsNotRealVst3();
    }
    if (filter == "missing-real-manifest") {
        return testMissingRealFixtureManifest(fixtures);
    }
    if (filter == "real-fixture") {
        return testRealFixtureManifestProductionHost(fixtures);
    }
    if (filter == "real-processing") {
        return testRealFixtureProcessing(fixtures);
    }
    if (filter == "real-parameters") {
        return testRealFixtureParameters(fixtures);
    }
    if (filter == "real-state") {
        return testRealFixtureStateRoundtrip(fixtures);
    }
    if (filter == "master-state") {
        return testMasterChainStateReload();
    }
    if (filter == "smoke") {
        return testSmokeSurface(fixtures);
    }
    if (testDeckProcessingChangesAudio() != 0 || testParametersAndLatency() != 0 || testDeterministicFixtureIsNotRealVst3() != 0 || testMissingRealFixtureManifest(fixtures) != 0 || testMasterChainStateReload() != 0 || testSmokeSurface(fixtures) != 0) {
        return 1;
    }
    std::cout << "VST3 processing tests passed\n";
    return 0;
}
