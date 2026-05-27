#include "audio/AudioDeckSmoke.h"
#include "decks/FourDeckPlaybackCore.h"

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

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    if (argc > 2) {
        return std::filesystem::path(argv[2]);
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

int loadFixtureDeck(djapp::decks::FourDeckPlaybackCore& core,
                    std::size_t deckIndex,
                    const std::filesystem::path& path) {
    djapp::decks::PreparedAudioMedia media;
    const auto fileLoad = djapp::decks::loadPcm16WavFileToPreparedMedia(path, media);
    if (expect(fileLoad.ok(), path.filename().string() + " should load as prepared fixture media") != 0) {
        return 1;
    }
    const auto deckId = djapp::core::DeckId::fromIndex(deckIndex).value;
    const auto load = core.loadDeck(deckId, djapp::decks::AudioDeckMediaReference::preparedAudio(std::move(media)));
    if (expect(load.ok(), "prepared fixture should load into deck") != 0) {
        return 1;
    }
    return expect(core.play(deckId) == djapp::decks::FourDeckPlaybackError::None, "deck should enter play state");
}

int testFourDeckCore() {
    using namespace djapp::audio;
    using namespace djapp::decks;

    FourDeckPlaybackCore core;
    for (std::size_t index = 0; index < djapp::audio::routing::kDeckCount; ++index) {
        const auto media = PreparedAudioMedia::deterministicTestWav(2048, 48000);
        const auto deckId = djapp::core::DeckId::fromIndex(index).value;
        if (expect(core.loadDeck(deckId, AudioDeckMediaReference::deterministicTestWav(media)).ok(), "deterministic deck load should succeed") != 0) {
            return 1;
        }
        if (expect(core.play(deckId) == FourDeckPlaybackError::None, "play should succeed") != 0) {
            return 1;
        }
    }

    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 2);
    if (expect(render.ok(), "four deck core render should succeed") != 0) {
        return 1;
    }
    if (expect(render.metrics.renderedFrames == 1024U, "four deck core should report rendered frames") != 0) {
        return 1;
    }
    if (expect(render.metrics.peakMagnitude > 0.01F, "four deck core should mix audible output") != 0) {
        return 1;
    }
    if (expect(render.metrics.maxCallbackMs <= kFourDeckCallbackBudgetMs, "four deck callback should stay under budget") != 0) {
        return 1;
    }

    const auto deckId = djapp::core::DeckId::fromIndex(0).value;
    if (expect(core.pause(deckId) == FourDeckPlaybackError::None, "pause should be typed") != 0) {
        return 1;
    }
    if (expect(!core.deck(deckId).state().transport.playing, "pause should stop transport without cueing") != 0) {
        return 1;
    }
    const auto pausedFrame = core.deck(deckId).playheadFrame();
    if (expect(pausedFrame > 0U, "pause should preserve playhead") != 0) {
        return 1;
    }
    if (expect(core.seek(deckId, 16) == FourDeckPlaybackError::None, "seek should be typed") != 0) {
        return 1;
    }
    if (expect(core.deck(deckId).playheadFrame() == 16U, "seek should update playhead") != 0) {
        return 1;
    }
    if (expect(core.cue(deckId) == FourDeckPlaybackError::None, "cue should be typed") != 0) {
        return 1;
    }
    if (expect(core.deck(deckId).playheadFrame() == 0U, "cue should return to start") != 0) {
        return 1;
    }

    std::cout << "JuceAudioDeck.FourDeckCore rendered=" << render.metrics.renderedFrames
              << " max-ms=" << render.metrics.maxCallbackMs << '\n';
    return 0;
}

int testFixtureRender(const std::filesystem::path& fixtures) {
    using namespace djapp::audio;
    using namespace djapp::decks;

    FourDeckPlaybackCore core;
    if (loadFixtureDeck(core, 0, fixtures / "track_120bpm.wav") != 0 ||
        loadFixtureDeck(core, 1, fixtures / "track_128bpm.wav") != 0 ||
        loadFixtureDeck(core, 2, fixtures / "silence_10s.wav") != 0 ||
        loadFixtureDeck(core, 3, fixtures / "track_120bpm.wav") != 0) {
        return 1;
    }

    const auto render = core.renderOffline(AudioRenderConfiguration{48000, 512}, 4);
    if (expect(render.ok(), "fixture render should succeed") != 0) {
        return 1;
    }
    if (expect(render.metrics.renderedFrames == 2048U, "fixture render should report rendered frames") != 0) {
        return 1;
    }
    if (expect(render.metrics.underrunFrames == 0U, "fixture render should not underrun") != 0) {
        return 1;
    }
    if (expect(render.metrics.maxCallbackMs <= kFourDeckCallbackBudgetMs, "fixture callback should stay under budget") != 0) {
        return 1;
    }

    std::cout << "JuceAudioDeck.FixtureRender rendered=" << render.metrics.renderedFrames
              << " underruns=" << render.metrics.underrunFrames
              << " max-ms=" << render.metrics.maxCallbackMs << '\n';
    return 0;
}

int testTypedErrors(const std::filesystem::path& fixtures) {
    using namespace djapp::decks;

    PreparedAudioMedia media;
    const auto missing = loadPcm16WavFileToPreparedMedia(fixtures / "missing.wav", media);
    if (expect(!missing.ok() && missing.error == AudioDeckLoadError::MissingMedia, "missing fixture should be typed") != 0) {
        return 1;
    }
    const auto corrupt = loadPcm16WavFileToPreparedMedia(fixtures / "corrupt_audio.wav", media);
    if (expect(!corrupt.ok() && corrupt.error == AudioDeckLoadError::CorruptMedia, "corrupt fixture should be typed") != 0) {
        return 1;
    }
    const auto unsupported = loadPcm16WavFileToPreparedMedia(fixtures / "not_audio.txt", media);
    if (expect(!unsupported.ok() && unsupported.error == AudioDeckLoadError::UnsupportedMedia, "non-audio fixture should be typed") != 0) {
        return 1;
    }

    std::cout << "JuceAudioDeck.Errors missing=" << toString(missing.error)
              << " corrupt=" << toString(corrupt.error)
              << " unsupported=" << toString(unsupported.error) << '\n';
    return 0;
}

int testSmokeSurface(const std::filesystem::path& fixtures) {
    std::ostringstream output;
    const auto result = djapp::audio::runAudioDeckSmokeTest(output, djapp::audio::AudioDeckSmokeOptions{fixtures});
    const auto text = output.str();
    if (expect(result == 0, "audio deck smoke surface should return success for prepared fixture render") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("rendered-frames=2048") != std::string::npos, "smoke output should report rendered frames") != 0) {
        return 1;
    }
    if (expect(text.find("callback-max-ms=") != std::string::npos, "smoke output should report callback metric") != 0) {
        return 1;
    }
    if (expect(text.find("typed-errors missing=missing-media") != std::string::npos, "smoke output should report typed errors") != 0) {
        return 1;
    }
    std::cout << "JuceAudioDeck.SmokeSurface captured=1\n";
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);

    if (filter == "core") {
        return testFourDeckCore();
    }
    if (filter == "fixture") {
        return testFixtureRender(fixtures);
    }
    if (filter == "errors") {
        return testTypedErrors(fixtures);
    }
    if (filter == "smoke") {
        return testSmokeSurface(fixtures);
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown JuceAudioDeck filter " << filter << '\n';
        return 1;
    }

    if (testFourDeckCore() != 0 || testFixtureRender(fixtures) != 0 || testTypedErrors(fixtures) != 0 || testSmokeSurface(fixtures) != 0) {
        return 1;
    }
    std::cout << "Juce audio deck tests passed\n";
    return 0;
}
