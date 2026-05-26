#pragma once

#include "audio/AudioEngine.h"
#include "core/DomainModels.h"
#include "midi/MidiBuffer.h"

#include <cstddef>
#include <cstdint>

namespace djapp::decks {

enum class StepResolution : std::uint8_t {
    Quarter,
    Eighth,
    Sixteenth,
};

struct MidiOutputTarget final {
    core::DeckId sourceDeck{};
    std::uint8_t pluginSlotIndex{};
    bool synthPlaceholder{true};
};

struct SequencerDeckState final {
    core::DeckId id{};
    core::DeckType type{core::DeckType::MidiStepSequencer};
    core::TransportState transport{};
    core::MidiStepPattern pattern{};
    StepResolution stepResolution{StepResolution::Sixteenth};
    MidiOutputTarget outputTarget{};
    std::uint8_t channel{};
};

struct SequencerRenderResult final {
    std::uint64_t renderedFrames{};
    double startBeat{};
    double endBeat{};
    double bpm{};
    MidiOutputTarget outputTarget{};
    midi::MidiBuffer midi{};
};

class MidiStepSequencerDeck final {
public:
    explicit MidiStepSequencerDeck(core::DeckId id) noexcept;

    [[nodiscard]] const SequencerDeckState& state() const noexcept;
    void setPattern(const core::MidiStepPattern& pattern) noexcept;
    void setStepResolution(StepResolution resolution) noexcept;
    void setOutputTarget(MidiOutputTarget target) noexcept;
    void play() noexcept;
    void stop() noexcept;
    void cueToStart() noexcept;

    [[nodiscard]] SequencerRenderResult renderMidiBlock(const audio::AudioRenderConfiguration& configuration,
                                                        const core::MasterClockState& clock) noexcept;

private:
    SequencerDeckState state_{};
};

[[nodiscard]] double beatsPerStep(StepResolution resolution) noexcept;
[[nodiscard]] double patternLengthBeats(StepResolution resolution) noexcept;

}
