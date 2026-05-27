#include "audio/AudioDeckSmoke.h"
#include "decks/FourDeckPlaybackCore.h"
#include "persistence/Persistence.h"

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
int expectOk(const deckflaxia::persistence::PersistenceResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int loadDeck(deckflaxia::decks::FourDeckPlaybackCore& core, std::size_t deckIndex, const std::filesystem::path& path) {
    deckflaxia::decks::PreparedAudioMedia media;
    const auto loaded = deckflaxia::decks::loadPcm16WavFileToPreparedMedia(path, media);
    if (expect(loaded.ok(), path.filename().string() + " should load") != 0) {
        return 1;
    }
    const auto deckId = deckflaxia::core::DeckId::fromIndex(deckIndex).value;
    if (expect(core.loadDeck(deckId, deckflaxia::decks::AudioDeckMediaReference::preparedAudio(std::move(media))).ok(), "deck load should succeed") != 0) {
        return 1;
    }
    return expect(core.play(deckId) == deckflaxia::decks::FourDeckPlaybackError::None, "deck play should succeed");
}

float rms(const deckflaxia::decks::FourDeckPlaybackCore& core) {
    const auto& buffer = core.lastInterleavedOutput();
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < 512U; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * deckflaxia::decks::kFourDeckOutputChannels;
        sum += static_cast<double>(buffer[index]) * static_cast<double>(buffer[index]);
        sum += static_cast<double>(buffer[index + 1U]) * static_cast<double>(buffer[index + 1U]);
    }
    return static_cast<float>(std::sqrt(sum / 1024.0));
}

float renderCrossfade(const std::filesystem::path& fixtures, float value) {
    deckflaxia::decks::FourDeckPlaybackCore core;
    if (loadDeck(core, 0, fixtures / "track_120bpm.wav") != 0 || loadDeck(core, 1, fixtures / "track_128bpm.wav") != 0) {
        return 0.0F;
    }
    if (expect(core.mixer().enqueue(deckflaxia::audio::MixerCommand{deckflaxia::audio::MixerCommandKind::SetCrossfader, 0, value, 0}).ok(), "crossfader command should queue") != 0) {
        return 0.0F;
    }
    if (expect(core.mixer().enqueue(deckflaxia::audio::MixerCommand{deckflaxia::audio::MixerCommandKind::SetDeckVolume, 1, 0.5F, 0}).ok(), "deck B volume command should queue") != 0) {
        return 0.0F;
    }
    const auto render = core.renderOffline(deckflaxia::audio::AudioRenderConfiguration{48000, 512}, 1);
    return render.ok() ? rms(core) : 0.0F;
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    if (argc > 2) {
        return std::filesystem::path(argv[2]);
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

int testCrossfader(const std::filesystem::path& fixtures) {
    const auto left = renderCrossfade(fixtures, 0.0F);
    const auto center = renderCrossfade(fixtures, 0.5F);
    const auto right = renderCrossfade(fixtures, 1.0F);
    if (expect(left > 0.001F && center > 0.001F && right > 0.001F, "crossfader positions should render measurable output") != 0) {
        return 1;
    }
    if (expect(std::abs(left - right) > 0.0001F || std::abs(center - left) > 0.0001F, "crossfader should change deck A/B output mix") != 0) {
        return 1;
    }
    std::cout << "MixerMidi.Crossfader left-rms=" << left << " center-rms=" << center << " right-rms=" << right << '\n';
    return 0;
}

int testMidiPersistenceDispatch() {
    using namespace deckflaxia::midi;
    using namespace deckflaxia::persistence;

    PersistenceService service;
    MidiLearnController controller;
    const auto play = controller.bind("deck.1.transport.play", MidiMessage::controlChange(0, 20, 127));
    const auto cue = controller.bind("deck.1.transport.cue", MidiMessage::controlChange(0, 21, 127));
    const auto crossfader = controller.bind("mixer.crossfader", MidiMessage::controlChange(0, 22, 64));
    const auto plugin = controller.bind("plugin.deck.1.slot.1.bypass", MidiMessage::controlChange(0, 23, 127));
    if (expect(play.ok() && cue.ok() && crossfader.ok() && plugin.ok(), "play cue crossfader plugin mappings should learn") != 0) {
        return 1;
    }
    (void)service.midiMappings().save(play.mapping);
    (void)service.midiMappings().save(cue.mapping);
    (void)service.midiMappings().save(crossfader.mapping);
    (void)service.midiMappings().save(plugin.mapping);
    const auto persisted = service.midiMappings().list();
    if (expectOk(persisted, "persisted MIDI mapping list") != 0 || expect(persisted.value.size() == 4U, "four task-9 MIDI mappings should persist") != 0) {
        return 1;
    }

    MidiLearnController reloaded;
    reloaded.loadMappings(persisted.value);
    const auto playDispatch = reloaded.dispatch(MidiMessage::controlChange(0, 20, 127));
    const auto cueDispatch = reloaded.dispatch(MidiMessage::controlChange(0, 21, 127));
    const auto crossfaderDispatch = reloaded.dispatch(MidiMessage::controlChange(0, 22, 64));
    const auto pluginDispatch = reloaded.dispatch(MidiMessage::controlChange(0, 23, 127));
    if (expect(playDispatch.dispatched() && cueDispatch.dispatched() && crossfaderDispatch.dispatched() && pluginDispatch.dispatched(), "reloaded mappings should dispatch") != 0) {
        return 1;
    }
    if (expect(playDispatch.command.kind == MidiTargetCommandKind::SetDeckTransport && playDispatch.command.parameterIndex == 1U, "play should dispatch transport intent") != 0) {
        return 1;
    }
    if (expect(cueDispatch.command.kind == MidiTargetCommandKind::SetDeckTransport && cueDispatch.command.parameterIndex == 2U, "cue should dispatch transport intent") != 0) {
        return 1;
    }
    if (expect(crossfaderDispatch.command.kind == MidiTargetCommandKind::SetCrossfader, "crossfader should dispatch mixer intent") != 0) {
        return 1;
    }
    if (expect(pluginDispatch.command.kind == MidiTargetCommandKind::SetPluginParameter, "plugin bypass should dispatch plugin-chain intent") != 0) {
        return 1;
    }

    deckflaxia::audio::MixerController mixer;
    deckflaxia::audio::routing::AudioRoutingGraphController routing(deckflaxia::audio::routing::RoutingDeviceLayout::forChannelCount(8));
    if (expect(mixer.enqueueFromMidi(playDispatch.command).ok(), "play MIDI intent should enqueue") != 0 ||
        expect(mixer.enqueueFromMidi(cueDispatch.command).ok(), "cue MIDI intent should enqueue") != 0 ||
        expect(mixer.enqueueFromMidi(crossfaderDispatch.command).ok(), "crossfader MIDI intent should enqueue") != 0 ||
        expect(mixer.enqueueFromMidi(pluginDispatch.command).ok(), "plugin MIDI intent should enqueue") != 0) {
        return 1;
    }
    if (expect(mixer.processPendingUpdatesOutsideCallback(routing).ok(), "MIDI intents should process outside callback") != 0) {
        return 1;
    }
    return expect(mixer.activeSnapshot().decks[0].pluginBypassed && mixer.activeSnapshot().crossfader > 0.49F,
                  "processed MIDI intents should update model snapshot");
}

int testSmokeSurface(const std::filesystem::path& fixtures) {
    std::ostringstream output;
    const auto result = deckflaxia::audio::runMixerSmokeTest(output, deckflaxia::audio::AudioDeckSmokeOptions{fixtures});
    const auto text = output.str();
    if (expect(result == 0, "mixer smoke should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    return expect(text.find("wav-render=omitted reason=") != std::string::npos && text.find("persisted-mappings=4") != std::string::npos,
                  "mixer smoke should report honest WAV omission and MIDI persistence");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto fixtures = fixtureDirectory(argc, argv);
    if (filter == "crossfader") {
        return testCrossfader(fixtures);
    }
    if (filter == "midi") {
        return testMidiPersistenceDispatch();
    }
    if (filter == "smoke") {
        return testSmokeSurface(fixtures);
    }
    if (testCrossfader(fixtures) != 0 || testMidiPersistenceDispatch() != 0 || testSmokeSurface(fixtures) != 0) {
        return 1;
    }
    std::cout << "Mixer MIDI tests passed\n";
    return 0;
}
