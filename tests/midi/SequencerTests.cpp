#include "decks/SequencerDeck.h"

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

bool eventMatches(const deckflaxia::midi::MidiBufferEvent& event,
                  std::uint32_t offset,
                  deckflaxia::midi::MidiMessageKind kind,
                  std::uint8_t note,
                  std::uint8_t velocity) {
    return event.sampleOffset == offset && event.message.kind == kind && event.message.note == note && event.message.velocity == velocity;
}

deckflaxia::core::MasterClockState runningClock(double bpm, double positionBeats) {
    auto clock = deckflaxia::core::MasterClockState::stopped(bpm);
    clock.positionBeats = positionBeats;
    clock.start();
    return clock;
}

deckflaxia::core::MidiStepPattern fourOnPattern() {
    auto pattern = deckflaxia::core::MidiStepPattern::sixteenStepDefault(60);
    pattern.steps[0] = deckflaxia::core::MidiStep{true, 60, 100, 0.25};
    pattern.steps[1] = deckflaxia::core::MidiStep{true, 62, 90, 0.25};
    pattern.steps[2] = deckflaxia::core::MidiStep{true, 64, 80, 0.25};
    pattern.steps[3] = deckflaxia::core::MidiStep{true, 65, 70, 0.25};
    return pattern;
}

int testDeterministicOffsets() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;
    using namespace deckflaxia::midi;

    MidiStepSequencerDeck deck(DeckId::fromIndex(0).value);
    deck.setPattern(fourOnPattern());
    deck.setStepResolution(StepResolution::Sixteenth);
    deck.setOutputTarget(MidiOutputTarget{DeckId::fromIndex(0).value, 2, true});

    const auto render = deck.renderMidiBlock(AudioRenderConfiguration{44100, 12000}, runningClock(120.0, 0.0));
    if (expect(render.midi.size() == 5, "120 BPM sixteenth block should schedule expected events") != 0) {
        return 1;
    }
    if (expect(eventMatches(render.midi.event(0), 0, MidiMessageKind::NoteOn, 60, 100), "step 0 note-on offset") != 0) {
        return 1;
    }
    if (expect(eventMatches(render.midi.event(1), 5513, MidiMessageKind::NoteOff, 60, 0), "step 0 note-off offset") != 0) {
        return 1;
    }
    if (expect(eventMatches(render.midi.event(2), 5513, MidiMessageKind::NoteOn, 62, 90), "step 1 note-on offset") != 0) {
        return 1;
    }
    if (expect(eventMatches(render.midi.event(3), 11025, MidiMessageKind::NoteOff, 62, 0), "step 1 note-off offset") != 0) {
        return 1;
    }
    if (expect(eventMatches(render.midi.event(4), 11025, MidiMessageKind::NoteOn, 64, 80), "step 2 note-on offset") != 0) {
        return 1;
    }
    if (expect(render.outputTarget.pluginSlotIndex == 2 && render.outputTarget.synthPlaceholder, "placeholder output target should be carried with MIDI") != 0) {
        return 1;
    }

    std::cout << "Sequencer.DeterministicOffsets offsets=";
    for (std::size_t index = 0; index < render.midi.size(); ++index) {
        std::cout << render.midi.event(index).sampleOffset << ':' << toString(render.midi.event(index).message.kind) << ':'
                  << static_cast<int>(render.midi.event(index).message.note);
        if (index + 1 < render.midi.size()) {
            std::cout << ',';
        }
    }
    std::cout << " target-slot=" << static_cast<int>(render.outputTarget.pluginSlotIndex) << '\n';
    return 0;
}

int testMutedStep() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;

    MidiStepSequencerDeck deck(DeckId::fromIndex(1).value);
    auto pattern = MidiStepPattern::sixteenStepDefault(36);
    pattern.steps[0] = MidiStep{false, 36, 127, 0.25};
    pattern.steps[1] = MidiStep{true, 38, 100, 0.25};
    deck.setPattern(pattern);

    const auto render = deck.renderMidiBlock(AudioRenderConfiguration{44100, 6000}, runningClock(120.0, 0.0));
    if (expect(render.midi.size() == 1, "muted first step should emit no MIDI while next enabled step emits") != 0) {
        return 1;
    }
    return expect(render.midi.event(0).message.note == 38 && render.midi.event(0).sampleOffset == 5513,
                  "enabled step after mute should keep deterministic offset");
}

int testTempoChange() {
    using namespace deckflaxia::audio;
    using namespace deckflaxia::core;
    using namespace deckflaxia::decks;
    using namespace deckflaxia::midi;

    MidiStepSequencerDeck deck(DeckId::fromIndex(2).value);
    deck.setPattern(fourOnPattern());
    deck.setStepResolution(StepResolution::Sixteenth);

    const auto beforeChange = deck.renderMidiBlock(AudioRenderConfiguration{44100, 6000}, runningClock(120.0, 0.0));
    const auto afterChange = deck.renderMidiBlock(AudioRenderConfiguration{44100, 12000}, runningClock(60.0, 0.25));

    if (expect(beforeChange.midi.size() == 3, "pre-change block should schedule boundary events at 120 BPM") != 0) {
        return 1;
    }
    if (expect(eventMatches(beforeChange.midi.event(1), 5513, MidiMessageKind::NoteOff, 60, 0), "pre-change note-off uses 120 BPM") != 0) {
        return 1;
    }
    if (expect(afterChange.midi.size() == 4, "post-change block should schedule carried note-off and future events at new tempo") != 0) {
        return 1;
    }
    if (expect(eventMatches(afterChange.midi.event(0), 0, MidiMessageKind::NoteOff, 60, 0), "carried note-off lands at new block boundary") != 0) {
        return 1;
    }
    if (expect(eventMatches(afterChange.midi.event(1), 0, MidiMessageKind::NoteOn, 62, 90), "future step starts at block boundary after tempo change") != 0) {
        return 1;
    }
    if (expect(eventMatches(afterChange.midi.event(2), 11025, MidiMessageKind::NoteOff, 62, 0), "future note-off uses 60 BPM offset") != 0) {
        return 1;
    }
    if (expect(eventMatches(afterChange.midi.event(3), 11025, MidiMessageKind::NoteOn, 64, 80), "future note-on uses 60 BPM offset") != 0) {
        return 1;
    }

    std::cout << "Sequencer.TempoChange before=" << beforeChange.midi.event(1).sampleOffset
              << " after=" << afterChange.midi.event(2).sampleOffset
              << " bpm=" << afterChange.bpm << '\n';
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "offsets") {
        return testDeterministicOffsets();
    }
    if (filter == "mute") {
        return testMutedStep();
    }
    if (filter == "tempo") {
        return testTempoChange();
    }

    if (testDeterministicOffsets() != 0 || testMutedStep() != 0 || testTempoChange() != 0) {
        return 1;
    }

    std::cout << "Sequencer tests passed\n";
    return 0;
}
