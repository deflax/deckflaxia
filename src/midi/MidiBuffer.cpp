#include "midi/MidiBuffer.h"

namespace djapp::midi {

bool MidiBuffer::addEvent(MidiMessage message, std::uint32_t sampleOffset, std::uint32_t blockFrames) noexcept {
    if (sampleOffset >= blockFrames || size_ >= events_.size()) {
        return false;
    }
    events_[size_] = MidiBufferEvent{sampleOffset, message};
    ++size_;
    return true;
}

void MidiBuffer::clear() noexcept {
    size_ = 0;
}

std::size_t MidiBuffer::size() const noexcept {
    return size_;
}

bool MidiBuffer::empty() const noexcept {
    return size_ == 0;
}

const MidiBufferEvent& MidiBuffer::event(std::size_t index) const noexcept {
    return events_[index];
}

const char* toString(MidiMessageKind kind) noexcept {
    switch (kind) {
    case MidiMessageKind::NoteOn:
        return "note-on";
    case MidiMessageKind::NoteOff:
        return "note-off";
    case MidiMessageKind::ControlChange:
        return "cc";
    }
    return "note-on";
}

}
