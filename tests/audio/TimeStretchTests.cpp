#include "audio/TimeStretchEngine.h"
#include "decks/FourDeckPlaybackCore.h"
#include "persistence/Persistence.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
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

template <typename Result>
int expectOk(const Result& result, const std::string& message) {
    return expect(result.ok(), message);
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    if (argc > 2) {
        return std::filesystem::path(argv[2]);
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

int loadFixtureDeck(deckflaxia::decks::FourDeckPlaybackCore& core,
                    std::size_t deckIndex,
                    const std::filesystem::path& path,
                    double sourceBpm,
                    double targetBpm,
                    bool pitchLock) {
    deckflaxia::decks::PreparedAudioMedia media;
    const auto fileLoad = deckflaxia::decks::loadPcm16WavFileToPreparedMedia(path, media);
    if (expectOk(fileLoad, path.filename().string() + " should decode") != 0) {
        return 1;
    }
    const auto deckId = deckflaxia::core::DeckId::fromIndex(deckIndex).value;
    if (expectOk(core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::preparedAudio(std::move(media))), "deck load") != 0) {
        return 1;
    }
    if (expect(core.syncTempo(deckId, sourceBpm, targetBpm, pitchLock) == deckflaxia::decks::FourDeckPlaybackError::None, "tempo sync should configure") != 0) {
        return 1;
    }
    return expect(core.play(deckId) == deckflaxia::decks::FourDeckPlaybackError::None, "deck should play");
}

int testTempoSyncFixtures(const std::filesystem::path& fixtures) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    if (loadFixtureDeck(core, 0, fixtures / "track_120bpm.wav", 120.0, 128.0, true) != 0 ||
        loadFixtureDeck(core, 1, fixtures / "track_128bpm.wav", 128.0, 128.0, true) != 0) {
        return 1;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 8);
    if (expectOk(render, "tempo sync render") != 0) {
        return 1;
    }
    const auto& deck0 = core.deck(deckflaxia::core::DeckId::fromIndex(0).value);
    const auto& deck1 = core.deck(deckflaxia::core::DeckId::fromIndex(1).value);
    const auto tempoDiff = std::abs(deck0.timeStretchStatus().effectiveTempoBpm - deck1.timeStretchStatus().effectiveTempoBpm);
    if (expect(tempoDiff <= 0.5, "synced effective tempo diff should be <=0.5 BPM") != 0) {
        return 1;
    }
    if (expect(deck0.fractionalPlayheadFrame() > deck1.fractionalPlayheadFrame(), "120 BPM deck should consume faster after sync to 128 BPM") != 0) {
        return 1;
    }
    if (expect(render.metrics.underrunFrames == 0U, "tempo sync render should not underrun") != 0) {
        return 1;
    }
    std::cout << "TimeStretch.TempoSync diff-bpm=" << tempoDiff
              << " deck0-tempo=" << deck0.timeStretchStatus().effectiveTempoBpm
              << " deck1-tempo=" << deck1.timeStretchStatus().effectiveTempoBpm
              << " engine=" << deckflaxia::audio::toString(deck0.timeStretchStatus().engine) << '\n';
    return 0;
}

int testPitchLockDrift(const std::filesystem::path& fixtures) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    if (loadFixtureDeck(core, 0, fixtures / "track_120bpm.wav", 120.0, 128.0, true) != 0) {
        return 1;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 4);
    const auto& status = core.deck(deckflaxia::core::DeckId::fromIndex(0).value).timeStretchStatus();
    const auto drift = std::abs(status.pitchDriftCents);
    if (expectOk(render, "pitch lock render") != 0 || expect(drift <= 10.0, "pitch-lock drift budget should be <=10 cents") != 0) {
        return 1;
    }
    std::cout << "TimeStretch.PitchLock drift-cents=" << drift << " fallback=" << status.fallback << '\n';
    return 0;
}

int testOverloadFallback(const std::filesystem::path& fixtures) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    if (loadFixtureDeck(core, 0, fixtures / "track_120bpm.wav", 120.0, 128.0, true) != 0) {
        return 1;
    }
    const auto deckId = deckflaxia::core::DeckId::fromIndex(0).value;
    if (expect(core.setTimeStretchBypass(deckId, true) == deckflaxia::decks::FourDeckPlaybackError::None, "bypass should configure") != 0) {
        return 1;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 2);
    const auto& status = core.deck(deckId).timeStretchStatus();
    if (expectOk(render, "overload fallback render") != 0 ||
        expect(status.bypassed, "overload should report bypass") != 0 ||
        expect(status.fallbackEvents > 0U, "overload should report fallback event") != 0) {
        return 1;
    }
    std::cout << "TimeStretch.Overload bypassed=" << status.bypassed
              << " fallback-events=" << status.fallbackEvents
              << " callback-ms=" << render.metrics.maxCallbackMs << '\n';
    return 0;
}

int testPersistenceRoundTrip() {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;

    PersistenceService service;
    const auto tempoPitch = TempoPitchSettings::fromValues(120.0, 128.0, true, true, -3.5, false).value;
    DeckStateRecord record{1,
                           DeckType::AudioFile,
                           RoutingAssignment::deckOutput(OutputBus::Output2, true).value,
                           TransportState{true, 8.0},
                           "track-120",
                           tempoPitch};
    if (expectOk(service.deckStates().save(record), "deck state save") != 0) {
        return 1;
    }
    const auto loaded = service.deckStates().load(1);
    if (expectOk(loaded, "deck state load") != 0) {
        return 1;
    }
    if (expect(std::abs(loaded.value.tempoPitch.sourceBpm - 120.0) < 0.000001 &&
                   std::abs(loaded.value.tempoPitch.targetBpm - 128.0) < 0.000001 &&
                   loaded.value.tempoPitch.tempoSyncEnabled &&
                   loaded.value.tempoPitch.pitchLockEnabled &&
                   std::abs(loaded.value.tempoPitch.pitchShiftCents + 3.5) < 0.000001,
               "tempo/pitch settings should round trip through deck state repository") != 0) {
        return 1;
    }
    std::cout << "TimeStretch.Persistence source=" << loaded.value.tempoPitch.sourceBpm
              << " target=" << loaded.value.tempoPitch.targetBpm
              << " pitch-cents=" << loaded.value.tempoPitch.pitchShiftCents << '\n';
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);

    if (filter == "tempo") {
        return testTempoSyncFixtures(fixtures);
    }
    if (filter == "pitch") {
        return testPitchLockDrift(fixtures);
    }
    if (filter == "overload") {
        return testOverloadFallback(fixtures);
    }
    if (filter == "persistence") {
        return testPersistenceRoundTrip();
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown TimeStretch filter " << filter << '\n';
        return 1;
    }

    if (testTempoSyncFixtures(fixtures) != 0 ||
        testPitchLockDrift(fixtures) != 0 ||
        testOverloadFallback(fixtures) != 0 ||
        testPersistenceRoundTrip() != 0) {
        return 1;
    }
    std::cout << "Time stretch tests passed\n";
    return 0;
}
