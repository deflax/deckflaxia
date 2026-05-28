#include "audio/AudioDeckSmoke.h"

#include "audio/JuceAudioDeviceEngine.h"
#include "decks/FourDeckPlaybackCore.h"
#include "persistence/Persistence.h"
#include "plugins/PluginChainProcessor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

namespace deckflaxia::audio {

namespace {

std::vector<std::filesystem::path> realVst3FixtureManifestCandidates(const std::filesystem::path& fixtureDirectory) {
    std::vector<std::filesystem::path> candidates;
    if (fixtureDirectory.extension() == ".json") {
        candidates.push_back(fixtureDirectory);
    }
    candidates.push_back(fixtureDirectory / "manifest.json");
    candidates.push_back(fixtureDirectory / "real-vst3-fixture" / "manifest.json");
    candidates.emplace_back("generated/real-vst3-fixture/manifest.json");
    candidates.emplace_back("build-juce/generated/real-vst3-fixture/manifest.json");
    return candidates;
}

plugins::RealVst3FixtureManifestResult discoverRealVst3FixtureManifest(const std::filesystem::path& fixtureDirectory,
                                                                        std::filesystem::path& selectedManifestPath) {
    plugins::RealVst3FixtureManifestResult missing;
    for (const auto& candidate : realVst3FixtureManifestCandidates(fixtureDirectory)) {
        const auto result = plugins::loadRealVst3FixtureManifest(candidate);
        if (result.ok()) {
            selectedManifestPath = candidate;
            return result;
        }
        if (missing.reason.empty()) {
            missing = result;
            selectedManifestPath = candidate;
        }
        if (result.error != plugins::RealVst3FixtureManifestError::ManifestMissing) {
            selectedManifestPath = candidate;
            return result;
        }
    }
    return missing;
}

std::array<std::filesystem::path, 4> fourDeckFixtureFiles(const std::filesystem::path& fixtureDirectory) {
    return {fixtureDirectory / "track_120bpm.wav",
            fixtureDirectory / "track_128bpm.wav",
            fixtureDirectory / "silence_10s.wav",
            fixtureDirectory / "track_120bpm.wav"};
}

int runPreparedCoreSmoke(std::ostream& output, const AudioDeckSmokeOptions& options) {
    decks::FourDeckPlaybackCore core;
    const auto files = fourDeckFixtureFiles(options.fixtureDirectory);
    for (std::size_t index = 0; index < files.size(); ++index) {
        decks::PreparedAudioMedia media;
        const auto fileLoad = decks::loadPcm16WavFileToPreparedMedia(files[index], media);
        if (!fileLoad.ok()) {
            output << "deck-" << index << " loaded=0 error=" << decks::toString(fileLoad.error) << '\n';
            return 1;
        }
        const auto deckId = core::DeckId::fromIndex(index).value;
        const auto load = core.loadDeck(deckId, decks::AudioDeckMediaReference::preparedAudio(std::move(media)));
        output << "deck-" << index << " loaded=" << (load.ok() ? 1 : 0)
               << " frames=" << core.deck(deckId).state().loadedFrameCount
               << " error=" << (load.ok() ? "none" : decks::toString(load.deckError)) << '\n';
        if (!load.ok()) {
            return 1;
        }
        if (core.play(deckId) != decks::FourDeckPlaybackError::None) {
            return 1;
        }
    }

    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 4);
    decks::PreparedAudioMedia missingMedia;
    const auto missing = decks::loadPcm16WavFileToPreparedMedia(options.fixtureDirectory / "missing.wav", missingMedia);
    decks::PreparedAudioMedia corruptMedia;
    const auto corrupt = decks::loadPcm16WavFileToPreparedMedia(options.fixtureDirectory / "corrupt_audio.wav", corruptMedia);
    decks::PreparedAudioMedia textMedia;
    const auto unsupported = decks::loadPcm16WavFileToPreparedMedia(options.fixtureDirectory / "not_audio.txt", textMedia);

    output << std::fixed << std::setprecision(3)
           << "rendered-frames=" << render.metrics.renderedFrames
           << " underrun-frames=" << render.metrics.underrunFrames
           << " underrun-callbacks=" << render.metrics.underrunCallbacks
           << " callback-max-ms=" << render.metrics.maxCallbackMs
           << " callback-budget-ms=" << decks::kFourDeckCallbackBudgetMs
           << " peak=" << render.metrics.peakMagnitude << '\n';
    output << "typed-errors missing=" << decks::toString(missing.error)
           << " corrupt=" << decks::toString(corrupt.error)
           << " unsupported=" << decks::toString(unsupported.error) << '\n';
    return render.ok() && render.metrics.renderedFrames == 2048U && render.metrics.maxCallbackMs <= decks::kFourDeckCallbackBudgetMs ? 0 : 1;
}

float masterRms(const decks::FourDeckPlaybackCore& core, std::uint32_t frames) {
    const auto& buffer = core.lastInterleavedOutput();
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * decks::kFourDeckOutputChannels;
        sum += static_cast<double>(buffer[index]) * static_cast<double>(buffer[index]);
        sum += static_cast<double>(buffer[index + 1U]) * static_cast<double>(buffer[index + 1U]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(frames * 2U)));
}

float masterPeak(const decks::FourDeckPlaybackCore& core, std::uint32_t frames) {
    const auto& buffer = core.lastInterleavedOutput();
    float peak = 0.0F;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * decks::kFourDeckOutputChannels;
        peak = std::max(peak, std::abs(buffer[index]));
        peak = std::max(peak, std::abs(buffer[index + 1U]));
    }
    return peak;
}

float renderPluginChainRms(const core::PluginDescriptor& plugin, plugins::PluginProcessingStatus& status, float& peak) {
    decks::FourDeckPlaybackCore core;
    const auto deckId = core::DeckId::fromIndex(0).value;
    auto media = decks::PreparedAudioMedia::deterministicTestWav(4096, 48000);
    if (!core.loadDeck(deckId, decks::AudioDeckMediaReference::deterministicTestWav(std::move(media))).ok()) {
        return 0.0F;
    }
    const auto configured = core.setDeckPluginChain(deckId, core::PluginChainDescriptor{"deck-a", {plugin}});
    if (!configured.ok() || core.play(deckId) != decks::FourDeckPlaybackError::None) {
        return 0.0F;
    }
    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 1);
    status = core.deckPluginChain(deckId).status();
    peak = masterPeak(core, 512);
    return render.ok() ? masterRms(core, 512) : 0.0F;
}

std::vector<float> deterministicPluginSmokeBuffer() {
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

double renderPluginHostRms(plugins::OfflinePluginChainHost& host) {
    auto buffer = deterministicPluginSmokeBuffer();
    const auto metrics = host.processReplacing(buffer.data(), 512, false);
    return metrics.outputRms;
}

struct Vst3SmokeHostEvidence final {
    plugins::PluginProcessingStatus status;
    plugins::PluginHostError configureError{plugins::PluginHostError::None};
    plugins::PluginHostError parameterSetError{plugins::PluginHostError::None};
    plugins::PluginHostError snapshotError{plugins::PluginHostError::None};
    plugins::PluginHostError reloadConfigureError{plugins::PluginHostError::None};
    plugins::PluginHostError restoreError{plugins::PluginHostError::None};
    double defaultRms{};
    double changedRms{};
    double restoredRms{};
    double parameterValue{};
    double restoredParameterValue{};
    std::size_t stateBytes{};
    bool parameterChanged{};
    bool stateRestored{};
};

Vst3SmokeHostEvidence collectVst3SmokeHostEvidence(const core::PluginDescriptor& plugin) {
    Vst3SmokeHostEvidence evidence;
    plugins::OfflinePluginChainHost host;
    const core::PluginChainDescriptor chain{"deck-a", {plugin}};
    const auto configured = host.configure(plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512);
    evidence.configureError = configured.error;
    evidence.status = host.status();
    if (!configured.ok()) {
        return evidence;
    }

    evidence.defaultRms = renderPluginHostRms(host);
    const auto parameterSet = host.setParameter(0, "gain", 0.25);
    evidence.parameterSetError = parameterSet.error;
    evidence.parameterValue = host.parameter(0, "gain");
    evidence.changedRms = renderPluginHostRms(host);
    evidence.parameterChanged = parameterSet.ok() && std::abs(evidence.parameterValue - 0.25) < 0.01 &&
                                evidence.defaultRms > 0.0 && evidence.changedRms > 0.0 && std::abs(evidence.defaultRms - evidence.changedRms) > 0.0001;

    plugins::PluginStateSnapshot snapshot;
    const auto snapshotted = host.snapshotState(0, snapshot);
    evidence.snapshotError = snapshotted.error;
    evidence.stateBytes = snapshot.bytes.size();
    plugins::OfflinePluginChainHost reloaded;
    const auto reconfigured = reloaded.configure(plugins::PluginChainTargetKind::Deck, chain, 48000.0, 512);
    evidence.reloadConfigureError = reconfigured.error;
    if (snapshotted.ok() && reconfigured.ok()) {
        const auto restored = reloaded.restoreState(0, snapshot);
        evidence.restoreError = restored.error;
        evidence.restoredParameterValue = reloaded.parameter(0, "gain");
        evidence.restoredRms = renderPluginHostRms(reloaded);
        evidence.stateRestored = restored.ok() && evidence.stateBytes > 0U && std::abs(evidence.restoredParameterValue - evidence.parameterValue) < 0.01 &&
                                 std::abs(evidence.restoredRms - evidence.changedRms) < 0.0001;
    }
    return evidence;
}

int loadCrossfadeDeck(decks::FourDeckPlaybackCore& core, std::size_t deckIndex, const std::filesystem::path& path) {
    decks::PreparedAudioMedia media;
    const auto fileLoad = decks::loadPcm16WavFileToPreparedMedia(path, media);
    if (!fileLoad.ok()) {
        return 1;
    }
    const auto deckId = core::DeckId::fromIndex(deckIndex).value;
    if (!core.loadDeck(deckId, decks::AudioDeckMediaReference::preparedAudio(std::move(media))).ok()) {
        return 1;
    }
    return core.play(deckId) == decks::FourDeckPlaybackError::None ? 0 : 1;
}

float renderCrossfaderRms(const AudioDeckSmokeOptions& options, float crossfader) {
    decks::FourDeckPlaybackCore core;
    if (loadCrossfadeDeck(core, 0, options.fixtureDirectory / "track_120bpm.wav") != 0 ||
        loadCrossfadeDeck(core, 1, options.fixtureDirectory / "track_128bpm.wav") != 0) {
        return 0.0F;
    }
    (void)core.mixer().enqueue(MixerCommand{MixerCommandKind::SetDeckVolume, 1, 0.5F, 0});
    (void)core.mixer().enqueue(MixerCommand{MixerCommandKind::SetCrossfader, 0, crossfader, 0});
    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 1);
    return render.ok() ? masterRms(core, 512) : 0.0F;
}

std::filesystem::path evidenceLogPath(const AudioDeckSmokeOptions& options, const char* fileName) {
    if (!options.renderPath.empty()) {
        return options.renderPath.parent_path() / fileName;
    }
    return std::filesystem::path(".omo/evidence/real-playable-juce") / fileName;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
    return output.good();
}

}

int runAudioDeckSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options) {
    output << "audio-deck-smoke-test: four-deck-render\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
#if DECKFLAXIA_HAS_JUCE
    output << "juce-audio-engine=available\n";
    JuceAudioDeviceDeckEngine engine;
    const auto files = fourDeckFixtureFiles(options.fixtureDirectory);
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto deckId = core::DeckId::fromIndex(index).value;
        const auto load = engine.loadFileToDeck(deckId, files[index]);
        output << "deck-" << index << " loaded=" << (load.ok() ? 1 : 0)
               << " frames=" << engine.core().deck(deckId).state().loadedFrameCount
               << " error=" << toString(load.error) << '\n';
        if (!load.ok()) {
            return 1;
        }
        if (engine.core().play(deckId) != decks::FourDeckPlaybackError::None) {
            return 1;
        }
    }

    const auto render = engine.core().renderOffline(AudioRenderConfiguration{48000, 512}, 4);
    const auto missing = engine.loadFileToDeck(core::DeckId::fromIndex(0).value, options.fixtureDirectory / "missing.wav");
    const auto corrupt = engine.loadFileToDeck(core::DeckId::fromIndex(0).value, options.fixtureDirectory / "corrupt_audio.wav");
    const auto unsupported = engine.loadFileToDeck(core::DeckId::fromIndex(0).value, options.fixtureDirectory / "not_audio.txt");

    output << std::fixed << std::setprecision(3)
           << "rendered-frames=" << render.metrics.renderedFrames
           << " underrun-frames=" << render.metrics.underrunFrames
           << " underrun-callbacks=" << render.metrics.underrunCallbacks
           << " callback-max-ms=" << render.metrics.maxCallbackMs
           << " callback-budget-ms=" << decks::kFourDeckCallbackBudgetMs
           << " peak=" << render.metrics.peakMagnitude << '\n';
    output << "typed-errors missing=" << toString(missing.error)
           << " corrupt=" << toString(corrupt.error)
           << " unsupported=" << toString(unsupported.error) << '\n';
    return render.ok() && render.metrics.renderedFrames == 2048U && render.metrics.maxCallbackMs <= decks::kFourDeckCallbackBudgetMs ? 0 : 1;
#else
    output << "juce-audio-engine=unavailable typed-error=" << toString(JuceAudioDeckError::JuceUnavailable) << '\n';
    return runPreparedCoreSmoke(output, options);
#endif
}


int runTempoSyncSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options) {
    output << "tempo-sync-smoke-test: time-stretch\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
#if DECKFLAXIA_HAS_JUCE
    output << "juce-audio-engine=available\n";
#else
    output << "juce-audio-engine=unavailable typed-error=" << toString(JuceAudioDeckError::JuceUnavailable) << '\n';
#endif
    output << "rubber-band=" << (rubberBandTimeStretchAvailable() ? "available" : "unavailable")
           << " engine=" << toString(primaryTimeStretchEngineKind())
           << " quality=guarded-fallback\n";

    decks::FourDeckPlaybackCore core;
    const std::array<std::filesystem::path, 2> files{options.fixtureDirectory / "track_120bpm.wav", options.fixtureDirectory / "track_128bpm.wav"};
    const std::array<double, 2> bpms{120.0, 128.0};
    for (std::size_t index = 0; index < files.size(); ++index) {
        decks::PreparedAudioMedia media;
        const auto fileLoad = decks::loadPcm16WavFileToPreparedMedia(files[index], media);
        if (!fileLoad.ok()) {
            output << "deck-" << index << " loaded=0 error=" << decks::toString(fileLoad.error) << '\n';
            return 1;
        }
        const auto deckId = core::DeckId::fromIndex(index).value;
        const auto load = core.loadDeck(deckId, decks::AudioDeckMediaReference::preparedAudio(std::move(media)));
        if (!load.ok() || core.syncTempo(deckId, bpms[index], 128.0, true) != decks::FourDeckPlaybackError::None || core.play(deckId) != decks::FourDeckPlaybackError::None) {
            output << "deck-" << index << " setup=0\n";
            return 1;
        }
    }

    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 8);
    const auto& deck0 = core.deck(core::DeckId::fromIndex(0).value);
    const auto& deck1 = core.deck(core::DeckId::fromIndex(1).value);
    const auto tempoDiff = std::abs(deck0.timeStretchStatus().effectiveTempoBpm - deck1.timeStretchStatus().effectiveTempoBpm);
    const auto pitchDrift = std::max(std::abs(deck0.timeStretchStatus().pitchDriftCents), std::abs(deck1.timeStretchStatus().pitchDriftCents));

    output << std::fixed << std::setprecision(3)
           << "effective-tempo-deck0-bpm=" << deck0.timeStretchStatus().effectiveTempoBpm
           << " effective-tempo-deck1-bpm=" << deck1.timeStretchStatus().effectiveTempoBpm
           << " synced-effective-tempo-diff-bpm=" << tempoDiff
           << " pitch-lock-drift-cents=" << pitchDrift
           << " latency-frames=" << std::max(deck0.timeStretchStatus().latencyFrames, deck1.timeStretchStatus().latencyFrames)
           << " preferred-start-pad-frames=" << std::max(deck0.timeStretchStatus().preferredStartPadFrames, deck1.timeStretchStatus().preferredStartPadFrames)
           << " start-delay-frames=" << std::max(deck0.timeStretchStatus().startDelayFrames, deck1.timeStretchStatus().startDelayFrames)
           << " underrun-frames=" << render.metrics.underrunFrames
           << " callback-max-ms=" << render.metrics.maxCallbackMs
           << " callback-budget-ms=" << decks::kFourDeckCallbackBudgetMs << '\n';
    return render.ok() && tempoDiff <= 0.5 && pitchDrift <= 10.0 && render.metrics.underrunFrames == 0U && render.metrics.maxCallbackMs <= decks::kFourDeckCallbackBudgetMs ? 0 : 1;
}

int runTimeStretchOverloadSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options) {
    output << "time-stretch-overload-smoke-test: fallback-bypass\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
    decks::FourDeckPlaybackCore core;
    decks::PreparedAudioMedia media;
    const auto fileLoad = decks::loadPcm16WavFileToPreparedMedia(options.fixtureDirectory / "track_120bpm.wav", media);
    if (!fileLoad.ok()) {
        output << "loaded=0 error=" << decks::toString(fileLoad.error) << '\n';
        return 1;
    }
    const auto deckId = core::DeckId::fromIndex(0).value;
    if (!core.loadDeck(deckId, decks::AudioDeckMediaReference::preparedAudio(std::move(media))).ok()) {
        output << "deck-load=0\n";
        return 1;
    }
    (void)core.syncTempo(deckId, 120.0, 128.0, true);
    (void)core.setTimeStretchBypass(deckId, true);
    (void)core.play(deckId);
    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 2);
    const auto& status = core.deck(deckId).timeStretchStatus();
    output << std::fixed << std::setprecision(3)
           << "engine=" << toString(status.engine)
           << " bypassed=" << (status.bypassed ? 1 : 0)
           << " fallback=" << (status.fallback ? 1 : 0)
           << " fallback-events=" << status.fallbackEvents
           << " effective-tempo-bpm=" << status.effectiveTempoBpm
           << " underrun-frames=" << render.metrics.underrunFrames
           << " callback-max-ms=" << render.metrics.maxCallbackMs
           << " callback-budget-ms=" << decks::kFourDeckCallbackBudgetMs << '\n';
    return render.ok() && status.bypassed && status.fallbackEvents > 0U && render.metrics.maxCallbackMs <= decks::kFourDeckCallbackBudgetMs ? 0 : 1;
}

int runMixerSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options) {
    output << "mixer-smoke-test: crossfader-midi\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
#if DECKFLAXIA_HAS_JUCE
    output << "juce-audio-engine=available\n";
#else
    output << "juce-audio-engine=unavailable typed-error=" << toString(JuceAudioDeckError::JuceUnavailable) << '\n';
#endif
    const auto leftRms = renderCrossfaderRms(options, 0.0F);
    const auto centerRms = renderCrossfaderRms(options, 0.5F);
    const auto rightRms = renderCrossfaderRms(options, 1.0F);
    const auto changed = std::abs(leftRms - rightRms) > 0.0001F || std::abs(centerRms - leftRms) > 0.0001F;

    std::ostringstream crossfadeLog;
    crossfadeLog << std::fixed << std::setprecision(6)
                 << "mixer-smoke-test: crossfader\n"
                 << "deck-a-ratio=" << (leftRms > 0.0F ? 1.0F : 0.0F) << " rms=" << leftRms << '\n'
                 << "center-ratio=" << (centerRms > 0.0F && leftRms > 0.0F ? centerRms / leftRms : 0.0F) << " rms=" << centerRms << '\n'
                 << "deck-b-ratio=" << (rightRms > 0.0F && leftRms > 0.0F ? rightRms / leftRms : 0.0F) << " rms=" << rightRms << '\n'
                 << "wav-render=omitted reason=fallback render surface currently exposes deterministic interleaved memory metrics but no WAV encoder dependency\n";

    persistence::PersistenceService service;
    midi::MidiLearnController midi;
    const auto play = midi.bind("deck.1.transport.play", midi::MidiMessage::controlChange(0, 10, 127));
    const auto cue = midi.bind("deck.1.transport.cue", midi::MidiMessage::controlChange(0, 11, 127));
    const auto crossfader = midi.bind("mixer.crossfader", midi::MidiMessage::controlChange(0, 12, 64));
    const auto plugin = midi.bind("plugin.deck.1.slot.1.bypass", midi::MidiMessage::controlChange(0, 13, 127));
    if (!play.ok() || !cue.ok() || !crossfader.ok() || !plugin.ok()) {
        output << "midi-learn=failed\n";
        return 1;
    }
    (void)service.midiMappings().save(play.mapping);
    (void)service.midiMappings().save(cue.mapping);
    (void)service.midiMappings().save(crossfader.mapping);
    (void)service.midiMappings().save(plugin.mapping);
    midi::MidiLearnController reloaded;
    const auto persisted = service.midiMappings().list();
    if (!persisted.ok()) {
        output << "midi-persistence=failed\n";
        return 1;
    }
    reloaded.loadMappings(persisted.value);
    const auto playDispatch = reloaded.dispatch(midi::MidiMessage::controlChange(0, 10, 127));
    const auto cueDispatch = reloaded.dispatch(midi::MidiMessage::controlChange(0, 11, 127));
    const auto crossfaderDispatch = reloaded.dispatch(midi::MidiMessage::controlChange(0, 12, 64));
    const auto pluginDispatch = reloaded.dispatch(midi::MidiMessage::controlChange(0, 13, 127));

    MixerController mixer;
    routing::AudioRoutingGraphController routing(routing::RoutingDeviceLayout::forChannelCount(8));
    const auto mixerPlay = mixer.enqueueFromMidi(playDispatch.command);
    const auto mixerCue = mixer.enqueueFromMidi(cueDispatch.command);
    const auto mixerCrossfader = mixer.enqueueFromMidi(crossfaderDispatch.command);
    const auto mixerPlugin = mixer.enqueueFromMidi(pluginDispatch.command);
    const auto processed = mixer.processPendingUpdatesOutsideCallback(routing);

    std::ostringstream midiLog;
    midiLog << "mixer-smoke-test: midi\n"
            << "persisted-mappings=" << persisted.value.size() << '\n'
            << "play-dispatched=" << (playDispatch.dispatched() ? 1 : 0) << " command=" << static_cast<int>(playDispatch.command.kind) << '\n'
            << "cue-dispatched=" << (cueDispatch.dispatched() ? 1 : 0) << " command=" << static_cast<int>(cueDispatch.command.kind) << '\n'
            << "crossfader-dispatched=" << (crossfaderDispatch.dispatched() ? 1 : 0) << " command=" << static_cast<int>(crossfaderDispatch.command.kind) << " value=" << crossfaderDispatch.command.normalizedValue << '\n'
            << "plugin-bypass-dispatched=" << (pluginDispatch.dispatched() ? 1 : 0) << " command=" << static_cast<int>(pluginDispatch.command.kind) << '\n'
            << "mixer-enqueue-errors=" << toString(mixerPlay.error) << ',' << toString(mixerCue.error) << ',' << toString(mixerCrossfader.error) << ',' << toString(mixerPlugin.error) << '\n'
            << "mixer-processed=" << (processed.ok() ? 1 : 0) << " crossfader=" << mixer.activeSnapshot().crossfader
            << " plugin-bypassed=" << (mixer.activeSnapshot().decks[0].pluginBypassed ? 1 : 0) << '\n';

    const auto crossfadePath = evidenceLogPath(options, "task-9-crossfade.log");
    const auto midiPath = evidenceLogPath(options, "task-9-midi.log");
    const auto wroteCrossfade = writeTextFile(crossfadePath, crossfadeLog.str());
    const auto wroteMidi = writeTextFile(midiPath, midiLog.str());

    output << crossfadeLog.str();
    output << midiLog.str();
    output << "crossfade-log=" << crossfadePath.string() << " wrote=" << (wroteCrossfade ? 1 : 0) << '\n';
    output << "midi-log=" << midiPath.string() << " wrote=" << (wroteMidi ? 1 : 0) << '\n';
    return changed && playDispatch.dispatched() && cueDispatch.dispatched() && crossfaderDispatch.dispatched() && pluginDispatch.dispatched() &&
                   mixerPlay.ok() && mixerCue.ok() && mixerCrossfader.ok() && mixerPlugin.ok() && processed.ok() && wroteCrossfade && wroteMidi
               ? 0
               : 1;
}

int runVst3ProcessingSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options) {
    output << "vst3-processing-smoke-test: " << options.chain << '\n';
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';
#if DECKFLAXIA_HAS_JUCE
    output << "juce-vst3-host=available real-vst3-success=requires-real-vst3-fixture\n";
#else
    output << "juce-vst3-host=unavailable fallback=deterministic-test-processor real-vst3-success=0\n";
#endif

    core::PluginDescriptor processedPlugin = plugins::makeDeterministicGainPlugin(0.35, false);
    core::PluginDescriptor bypassPlugin = plugins::makeDeterministicGainPlugin(0.35, true);
#if DECKFLAXIA_HAS_JUCE
    std::filesystem::path manifestPath;
    const auto manifest = discoverRealVst3FixtureManifest(options.fixtureDirectory, manifestPath);
    output << "real-vst3-fixture-manifest=" << manifestPath.string()
           << " loaded=" << (manifest.ok() ? 1 : 0)
           << " error=" << plugins::toString(manifest.error) << '\n';
    if (!manifest.ok()) {
        output << "real-vst3-fixture-reason=" << manifest.reason << '\n';
        return 1;
    }
    processedPlugin = plugins::makeRealVst3FixturePlugin(manifest.manifest, false);
    bypassPlugin = plugins::makeRealVst3FixturePlugin(manifest.manifest, true);
    output << "real-vst3-fixture-id=" << manifest.manifest.fixtureId
           << " bundle-path=" << manifest.manifest.bundlePath.string()
           << " descriptor=" << processedPlugin.identifier << '\n';
#endif

    plugins::PluginProcessingStatus processedStatus;
    plugins::PluginProcessingStatus bypassStatus;
    float processedPeak = 0.0F;
    float bypassPeak = 0.0F;
    const auto processedRms = renderPluginChainRms(processedPlugin, processedStatus, processedPeak);
    const auto bypassRms = renderPluginChainRms(bypassPlugin, bypassStatus, bypassPeak);
    const auto changed = processedRms > 0.0F && bypassRms > 0.0F && std::abs(processedRms - bypassRms) > 0.0001F;
    const auto hostEvidence = collectVst3SmokeHostEvidence(processedPlugin);
    const auto realParameters = hostEvidence.status.realVst3Instantiated && hostEvidence.parameterChanged;
    const auto realState = hostEvidence.status.realVst3Instantiated && hostEvidence.stateRestored;

    persistence::PersistenceService service;
    auto masterPlugin = plugins::makeDeterministicGainPlugin(0.42, false);
    masterPlugin.latencyFrames = processedStatus.latencyFrames;
    const core::PluginChainDescriptor masterChain{"master", {masterPlugin}};
    const auto saved = service.pluginChains().save(masterChain);
    const auto loaded = service.pluginChains().load("master");
    const auto identical = saved.ok() && loaded.ok() && loaded.value.plugins.size() == 1U &&
                           loaded.value.plugins[0].identifier == masterPlugin.identifier &&
                           loaded.value.plugins[0].parameters.size() == 1U &&
                           std::abs(loaded.value.plugins[0].parameters[0].normalizedValue - 0.42) < 0.000001;

    std::ostringstream deckLog;
    deckLog << std::fixed << std::setprecision(6)
            << "vst3-processing-smoke-test: deck-a\n"
            << "backend=" << plugins::toString(processedStatus.backend) << " juce=" << (processedStatus.juceAvailable ? 1 : 0)
            << " real-vst3-instantiated=" << (processedStatus.realVst3Instantiated ? 1 : 0) << '\n'
            << "processed-rms=" << processedRms << " bypass-rms=" << bypassRms
            << " processed-peak=" << processedPeak << " bypass-peak=" << bypassPeak
            << " changed-audio=" << (changed ? 1 : 0) << '\n'
            << "real-parameters=" << (realParameters ? 1 : 0)
            << " parameter-marker=" << (hostEvidence.parameterChanged ? 1 : 0)
            << " parameter-configure-error=" << plugins::toString(hostEvidence.configureError)
            << " parameter-set-error=" << plugins::toString(hostEvidence.parameterSetError)
            << " default-rms=" << hostEvidence.defaultRms
            << " changed-rms=" << hostEvidence.changedRms
            << " gain=" << hostEvidence.parameterValue << '\n'
            << "real-state=" << (realState ? 1 : 0)
            << " state-marker=" << (hostEvidence.stateRestored ? 1 : 0)
            << " snapshot-error=" << plugins::toString(hostEvidence.snapshotError)
            << " reload-configure-error=" << plugins::toString(hostEvidence.reloadConfigureError)
            << " restore-error=" << plugins::toString(hostEvidence.restoreError)
            << " state-bytes=" << hostEvidence.stateBytes
            << " restored-rms=" << hostEvidence.restoredRms
            << " restored-gain=" << hostEvidence.restoredParameterValue << '\n'
            << "latency-frames=" << processedStatus.latencyFrames
            << " active-slots=" << processedStatus.activeSlotCount
            << " unavailable-slots=" << processedStatus.unavailableSlotCount << '\n'
            << "wav-render=omitted reason=fallback render surface currently exposes deterministic interleaved memory metrics but no WAV encoder dependency\n";
    if (!processedStatus.unavailableReason.empty()) {
        deckLog << "unavailable-reason=" << processedStatus.unavailableReason << '\n';
    }

    std::ostringstream masterLog;
    masterLog << std::fixed << std::setprecision(6)
              << "vst3-processing-smoke-test: master-state\n"
              << "saved=" << (saved.ok() ? 1 : 0)
              << " loaded=" << (loaded.ok() ? 1 : 0)
              << " identical=" << (identical ? 1 : 0) << '\n';
    if (loaded.ok() && !loaded.value.plugins.empty() && !loaded.value.plugins[0].parameters.empty()) {
        masterLog << "plugin-id=" << loaded.value.plugins[0].identifier
                  << " parameter=" << loaded.value.plugins[0].parameters[0].identifier
                  << " value=" << loaded.value.plugins[0].parameters[0].normalizedValue
                  << " latency-frames=" << loaded.value.plugins[0].latencyFrames << '\n';
    }

    const auto deckPath = evidenceLogPath(options, "task-10-deck-vst3.log");
    const auto masterPath = evidenceLogPath(options, "task-10-master-state.log");
    const auto wroteDeck = writeTextFile(deckPath, deckLog.str());
    const auto wroteMaster = writeTextFile(masterPath, masterLog.str());
    output << deckLog.str();
    output << masterLog.str();
    output << "deck-log=" << deckPath.string() << " wrote=" << (wroteDeck ? 1 : 0) << '\n';
    output << "master-log=" << masterPath.string() << " wrote=" << (wroteMaster ? 1 : 0) << '\n';
#if DECKFLAXIA_HAS_JUCE
    return changed && identical && realParameters && realState && wroteDeck && wroteMaster ? 0 : 1;
#else
    return changed && identical && wroteDeck && wroteMaster ? 0 : 1;
#endif
}

}
