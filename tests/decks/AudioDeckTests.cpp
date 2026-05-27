#include "audio/routing/AudioRoutingGraph.h"
#include "decks/AudioDeck.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int expectLoadOk(const deckflaxia::decks::AudioDeckLoadResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectLoadError(const deckflaxia::decks::AudioDeckLoadResult& result,
                    deckflaxia::decks::AudioDeckLoadError error,
                    const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == error, message + " should expose typed error");
}

int testValidFileRender() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;

    AudioRoutingGraphController routing(RoutingDeviceLayout::forChannelCount(4));
    const auto deckId = DeckId::fromIndex(0).value;
    if (expect(routing.enqueueAssignDeckOutput(deckId, OutputBus::Output2).ok(), "output assignment should queue") != 0) {
        return 1;
    }
    if (expect(routing.enqueueSetCueEnabled(deckId, true).ok(), "cue assignment should queue") != 0) {
        return 1;
    }
    if (expect(routing.processPendingUpdatesOutsideCallback().ok(), "routing updates should process outside callback") != 0) {
        return 1;
    }

    AudioFileDeck deck(deckId);
    const auto media = AudioDeckMediaReference::deterministicTestWav(PreparedAudioMedia::deterministicTestWav(256, 44100));
    if (expectLoadOk(deck.loadPreparedMedia(media), "deterministic test wav load") != 0) {
        return 1;
    }
    deck.cueToStart();
    deck.play();
    const auto render = deck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 2, routing.captureSnapshotForAudioCallback());

    if (expect(render.renderedFrames == 128, "rendered frame count should be deterministic") != 0) {
        return 1;
    }
    if (expect(render.peakMagnitude > 0.01F, "valid deterministic wav should render non-silent output") != 0) {
        return 1;
    }
    if (expect(render.routing.mainOutput == OutputBus::Output2, "deck render should observe output assignment") != 0) {
        return 1;
    }
    if (expect(render.assignedOutput == StereoOutputPair{2, 3}, "output 2 should map to channels 2/3") != 0) {
        return 1;
    }
    if (expect(render.cueEnabled, "deck render should observe cue assignment") != 0) {
        return 1;
    }
    if (expect(deck.state().transport.playing, "partial render should leave deck playing") != 0) {
        return 1;
    }

    std::cout << "AudioDeck.ValidFile peak=" << render.peakMagnitude << " frames=" << render.renderedFrames << '\n';
    return 0;
}

int testCorruptFilePreservesState() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;

    AudioRoutingGraphController routing(RoutingDeviceLayout::forChannelCount(2));
    const auto deckId = DeckId::fromIndex(1).value;
    AudioFileDeck deck(deckId);
    const auto validMedia = AudioDeckMediaReference::deterministicTestWav(PreparedAudioMedia::deterministicTestWav(128, 44100));
    if (expectLoadOk(deck.loadPreparedMedia(validMedia), "initial deterministic test wav load") != 0) {
        return 1;
    }
    deck.play();
    const auto before = deck.state();

    if (expectLoadError(deck.loadPreparedMedia(AudioDeckMediaReference::corrupt()),
                        AudioDeckLoadError::CorruptMedia,
                        "corrupt media") != 0) {
        return 1;
    }
    if (expectLoadError(deck.loadPreparedMedia(AudioDeckMediaReference::unsupported()),
                        AudioDeckLoadError::UnsupportedMedia,
                        "unsupported media") != 0) {
        return 1;
    }
    if (expectLoadError(deck.loadPreparedMedia(AudioDeckMediaReference::missing()),
                        AudioDeckLoadError::MissingMedia,
                        "missing media") != 0) {
        return 1;
    }

    const auto after = deck.state();
    if (expect(after.loaded == before.loaded, "failed loads should preserve loaded flag") != 0) {
        return 1;
    }
    if (expect(after.loadedFrameCount == before.loadedFrameCount, "failed loads should preserve frame count") != 0) {
        return 1;
    }
    if (expect(after.transport.playing == before.transport.playing, "failed loads should preserve play state") != 0) {
        return 1;
    }

    const auto render = deck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 1, routing.captureSnapshotForAudioCallback());
    if (expect(render.peakMagnitude > 0.01F, "previous valid media should remain renderable after failed loads") != 0) {
        return 1;
    }

    std::cout << "AudioDeck.CorruptFile errors=" << toString(AudioDeckLoadError::CorruptMedia) << ','
              << toString(AudioDeckLoadError::UnsupportedMedia) << ','
              << toString(AudioDeckLoadError::MissingMedia) << " preserved=1 peak=" << render.peakMagnitude << '\n';
    return 0;
}

int testTransportStopCue() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::audio::routing;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;

    const auto deckId = DeckId::fromIndex(2).value;
    AudioFileDeck deck(deckId);
    if (expectLoadOk(deck.loadPreparedMedia(AudioDeckMediaReference::deterministicTestWav(PreparedAudioMedia::deterministicTestWav(64, 44100))),
                     "transport media load") != 0) {
        return 1;
    }
    deck.play();
    AudioRoutingGraphController routing(RoutingDeviceLayout::forChannelCount(2));
    const auto playingRender = deck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 1, routing.captureSnapshotForAudioCallback());
    if (expect(playingRender.peakMagnitude > 0.01F, "playing render should be audible") != 0) {
        return 1;
    }
    deck.stop();
    const auto stoppedRender = deck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 1, routing.captureSnapshotForAudioCallback());
    if (expect(std::abs(stoppedRender.peakMagnitude) < 0.000001F, "stopped render should be silent") != 0) {
        return 1;
    }
    deck.cueToStart();
    deck.play();
    const auto cuedRender = deck.renderFromPreparedMedia(AudioRenderConfiguration{44100, 64}, 1, routing.captureSnapshotForAudioCallback());
    return expect(cuedRender.peakMagnitude > 0.01F, "cue to start should make media renderable again");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "valid") {
        return testValidFileRender();
    }
    if (filter == "corrupt") {
        return testCorruptFilePreservesState();
    }
    if (filter == "transport") {
        return testTransportStopCue();
    }

    if (testValidFileRender() != 0 || testCorruptFilePreservesState() != 0 || testTransportStopCue() != 0) {
        return 1;
    }

    std::cout << "Audio deck tests passed\n";
    return 0;
}
