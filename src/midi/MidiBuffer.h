#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace djapp::midi {

constexpr std::size_t kMaxMidiEventsPerBlock = 128;

enum class MidiMessageKind : std::uint8_t {
    NoteOn,
    NoteOff,
    ControlChange,
};

struct MidiMessage final {
    MidiMessageKind kind{MidiMessageKind::NoteOn};
    std::uint8_t channel{0};
    std::uint8_t note{60};
    std::uint8_t velocity{100};

    [[nodiscard]] static constexpr MidiMessage noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) noexcept {
        return MidiMessage{MidiMessageKind::NoteOn, channel, note, velocity};
    }

    [[nodiscard]] static constexpr MidiMessage noteOff(std::uint8_t channel, std::uint8_t note) noexcept {
        return MidiMessage{MidiMessageKind::NoteOff, channel, note, 0};
    }

    [[nodiscard]] static constexpr MidiMessage controlChange(std::uint8_t channel, std::uint8_t controller, std::uint8_t value) noexcept {
        return MidiMessage{MidiMessageKind::ControlChange, channel, controller, value};
    }

    [[nodiscard]] constexpr std::uint8_t controller() const noexcept { return note; }
    [[nodiscard]] constexpr std::uint8_t value() const noexcept { return velocity; }
};

struct MidiBufferEvent final {
    std::uint32_t sampleOffset{};
    MidiMessage message{};
};

class MidiBuffer final {
public:
    [[nodiscard]] bool addEvent(MidiMessage message, std::uint32_t sampleOffset, std::uint32_t blockFrames) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const MidiBufferEvent& event(std::size_t index) const noexcept;

private:
    std::array<MidiBufferEvent, kMaxMidiEventsPerBlock> events_{};
    std::size_t size_{};
};

[[nodiscard]] const char* toString(MidiMessageKind kind) noexcept;

}
