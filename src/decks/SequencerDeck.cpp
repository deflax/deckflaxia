#include "decks/SequencerDeck.h"

#include <algorithm>
#include <cmath>

namespace deckflaxia::decks {

namespace {

constexpr double kBeatEpsilon = 0.000000001;

std::uint8_t clampMidiByte(int value) noexcept {
    return static_cast<std::uint8_t>(std::max(0, std::min(127, value)));
}

std::uint32_t sampleOffsetForBeat(double eventBeat,
                                  double blockStartBeat,
                                  double bpm,
                                  std::uint32_t sampleRateHz) noexcept {
    const double seconds = (eventBeat - blockStartBeat) * 60.0 / bpm;
    return static_cast<std::uint32_t>(std::llround(seconds * static_cast<double>(sampleRateHz)));
}

bool beatInBlock(double eventBeat, double blockStartBeat, double blockEndBeat) noexcept {
    return eventBeat + kBeatEpsilon >= blockStartBeat && eventBeat < blockEndBeat - kBeatEpsilon;
}

void scheduleStepEvent(const core::MidiStep& step,
                       double eventBeat,
                       double blockStartBeat,
                       double blockEndBeat,
                       double bpm,
                       const audio::AudioRenderConfiguration& configuration,
                       midi::MidiBuffer& buffer,
                       bool noteOn,
                       std::uint8_t channel) noexcept {
    if (!beatInBlock(eventBeat, blockStartBeat, blockEndBeat)) {
        return;
    }

    const auto offset = sampleOffsetForBeat(eventBeat, blockStartBeat, bpm, configuration.sampleRateHz);
    if (noteOn) {
        (void)buffer.addEvent(midi::MidiMessage::noteOn(channel, clampMidiByte(step.note), clampMidiByte(step.velocity)), offset, configuration.bufferFrames);
    } else {
        (void)buffer.addEvent(midi::MidiMessage::noteOff(channel, clampMidiByte(step.note)), offset, configuration.bufferFrames);
    }
}

} // namespace

MidiStepSequencerDeck::MidiStepSequencerDeck(core::DeckId id) noexcept {
    state_.id = id;
    state_.type = core::DeckType::MidiStepSequencer;
    state_.pattern = core::MidiStepPattern::sixteenStepDefault(60);
    state_.outputTarget = MidiOutputTarget{id, 0, true};
}

const SequencerDeckState& MidiStepSequencerDeck::state() const noexcept {
    return state_;
}

void MidiStepSequencerDeck::setPattern(const core::MidiStepPattern& pattern) noexcept {
    state_.pattern = pattern;
}

void MidiStepSequencerDeck::setStepResolution(StepResolution resolution) noexcept {
    state_.stepResolution = resolution;
}

void MidiStepSequencerDeck::setOutputTarget(MidiOutputTarget target) noexcept {
    state_.outputTarget = target;
}

void MidiStepSequencerDeck::play() noexcept {
    state_.transport.playing = true;
}

void MidiStepSequencerDeck::stop() noexcept {
    state_.transport.playing = false;
}

void MidiStepSequencerDeck::cueToStart() noexcept {
    state_.transport.positionBeats = 0.0;
}

SequencerRenderResult MidiStepSequencerDeck::renderMidiBlock(const audio::AudioRenderConfiguration& configuration,
                                                            const core::MasterClockState& clock) noexcept {
    SequencerRenderResult result;
    result.renderedFrames = configuration.bufferFrames;
    result.startBeat = clock.positionBeats;
    result.bpm = clock.bpm;
    result.outputTarget = state_.outputTarget;

    const double blockBeats = static_cast<double>(configuration.bufferFrames) * clock.bpm /
                              (60.0 * static_cast<double>(configuration.sampleRateHz));
    result.endBeat = result.startBeat + blockBeats;
    state_.transport.playing = clock.playing;
    state_.transport.positionBeats = clock.positionBeats;

    if (!clock.playing || !state_.transport.playing || clock.bpm <= 0.0 || configuration.sampleRateHz == 0U || configuration.bufferFrames == 0U) {
        return result;
    }

    const double stepBeats = beatsPerStep(state_.stepResolution);
    const double cycleBeats = patternLengthBeats(state_.stepResolution);
    const auto firstCycle = static_cast<long long>(std::floor(result.startBeat / cycleBeats)) - 1;
    const auto lastCycle = static_cast<long long>(std::floor(result.endBeat / cycleBeats)) + 1;

    for (long long cycle = firstCycle; cycle <= lastCycle; ++cycle) {
        const double cycleStartBeat = static_cast<double>(cycle) * cycleBeats;
        for (std::size_t stepIndex = 0; stepIndex < state_.pattern.steps.size(); ++stepIndex) {
            const auto& step = state_.pattern.steps[stepIndex];
            if (!step.enabled) {
                continue;
            }

            const double stepStartBeat = cycleStartBeat + (static_cast<double>(stepIndex) * stepBeats);
            const double stepEndBeat = stepStartBeat + std::max(0.0, step.lengthBeats);
            scheduleStepEvent(step, stepStartBeat, result.startBeat, result.endBeat, clock.bpm, configuration, result.midi, true, state_.channel);
            scheduleStepEvent(step, stepEndBeat, result.startBeat, result.endBeat, clock.bpm, configuration, result.midi, false, state_.channel);
        }
    }

    return result;
}

double beatsPerStep(StepResolution resolution) noexcept {
    switch (resolution) {
    case StepResolution::Quarter:
        return 1.0;
    case StepResolution::Eighth:
        return 0.5;
    case StepResolution::Sixteenth:
        return 0.25;
    }
    return 0.25;
}

double patternLengthBeats(StepResolution resolution) noexcept {
    return beatsPerStep(resolution) * 16.0;
}

}
