#include "audio/MixerControls.h"

#include <algorithm>
#include <cmath>

namespace deckflaxia::audio {

namespace {

[[nodiscard]] bool validDeckIndex(std::uint32_t deckIndex) noexcept {
    return deckIndex < kMixerDeckCount;
}

[[nodiscard]] float clamp01(float value) noexcept {
    return std::max(0.0F, std::min(1.0F, value));
}

[[nodiscard]] float clampEq(float value) noexcept {
    return std::max(0.0F, std::min(2.0F, value));
}

[[nodiscard]] float averageEq(const MixerDeckSnapshot& deck) noexcept {
    return (deck.eqLow + deck.eqMid + deck.eqHigh) / 3.0F;
}

[[nodiscard]] core::OutputBus outputFromNormalizedValue(float value) noexcept {
    if (value < 0.2F) {
        return core::OutputBus::Master;
    }
    if (value < 0.4F) {
        return core::OutputBus::Output1;
    }
    if (value < 0.6F) {
        return core::OutputBus::Output2;
    }
    if (value < 0.8F) {
        return core::OutputBus::Output3;
    }
    return core::OutputBus::Output4;
}

[[nodiscard]] MixerCommandKind mixerKindFromMidi(const midi::MidiTargetCommand& command) noexcept {
    switch (command.kind) {
    case midi::MidiTargetCommandKind::SetDeckTransport:
        return command.parameterIndex == 2U ? MixerCommandKind::SetTransportCue : MixerCommandKind::SetTransportPlay;
    case midi::MidiTargetCommandKind::SetDeckGain:
        return MixerCommandKind::SetDeckGain;
    case midi::MidiTargetCommandKind::SetDeckOutput:
        return command.parameterIndex == 1U ? MixerCommandKind::SetCueEnabled : MixerCommandKind::SetDeckOutput;
    case midi::MidiTargetCommandKind::SetPluginParameter:
        return MixerCommandKind::TogglePluginBypass;
    case midi::MidiTargetCommandKind::SetCrossfader:
        return MixerCommandKind::SetCrossfader;
    case midi::MidiTargetCommandKind::SetDeckVolume:
        return MixerCommandKind::SetDeckVolume;
    case midi::MidiTargetCommandKind::SetDeckEqLow:
        return MixerCommandKind::SetDeckEqLow;
    case midi::MidiTargetCommandKind::SetDeckEqMid:
        return MixerCommandKind::SetDeckEqMid;
    case midi::MidiTargetCommandKind::SetDeckEqHigh:
        return MixerCommandKind::SetDeckEqHigh;
    case midi::MidiTargetCommandKind::SetSequencerControl:
    case midi::MidiTargetCommandKind::TriggerLibraryAction:
        return MixerCommandKind::SetDeckVolume;
    }
    return MixerCommandKind::SetDeckVolume;
}

}

bool FixedMixerCommandQueue::tryPushFromMessageThread(const AudioGraphCommand& command) noexcept {
    MixerCommand mixerCommand;
    mixerCommand.deckIndex = command.targetId;
    mixerCommand.value = command.value;
    mixerCommand.aux = command.aux;
    switch (command.kind) {
    case AudioGraphCommandKind::SetDeckGain:
        mixerCommand.kind = MixerCommandKind::SetDeckGain;
        break;
    case AudioGraphCommandKind::AssignDeckOutput:
        mixerCommand.kind = MixerCommandKind::SetDeckOutput;
        break;
    case AudioGraphCommandKind::SetCueEnabled:
        mixerCommand.kind = MixerCommandKind::SetCueEnabled;
        break;
    case AudioGraphCommandKind::SetTransportState:
        mixerCommand.kind = MixerCommandKind::SetTransportPlay;
        break;
    case AudioGraphCommandKind::InsertPluginSlot:
    case AudioGraphCommandKind::RemovePluginSlot:
    case AudioGraphCommandKind::ReplaceSnapshot:
        return false;
    }
    return tryPushMixerCommand(mixerCommand);
}

bool FixedMixerCommandQueue::tryPopForAudioThread(AudioGraphCommand& command) noexcept {
    if (audioReadIndex_ == writeIndex_) {
        return false;
    }
    const auto mixerCommand = commands_[audioReadIndex_];
    ++audioReadIndex_;
    command = AudioGraphCommand{AudioGraphCommandKind::SetDeckGain, mixerCommand.deckIndex, mixerCommand.value, mixerCommand.aux};
    return true;
}

bool FixedMixerCommandQueue::tryPushMixerCommand(const MixerCommand& command) noexcept {
    if (writeIndex_ == commands_.size()) {
        return false;
    }
    commands_[writeIndex_] = command;
    ++writeIndex_;
    return true;
}

bool FixedMixerCommandQueue::tryPopForMixerUpdate(MixerCommand& command) noexcept {
    if (updateReadIndex_ == writeIndex_) {
        return false;
    }
    command = commands_[updateReadIndex_];
    ++updateReadIndex_;
    if (audioReadIndex_ < updateReadIndex_) {
        audioReadIndex_ = updateReadIndex_;
    }
    if (updateReadIndex_ == writeIndex_) {
        writeIndex_ = 0;
        audioReadIndex_ = 0;
        updateReadIndex_ = 0;
    }
    return true;
}

std::size_t FixedMixerCommandQueue::pendingCount() const noexcept {
    return writeIndex_ - updateReadIndex_;
}

const MixerSnapshot& MixerController::activeSnapshot() const noexcept {
    return activeSnapshot_;
}

MixerSnapshot MixerController::captureSnapshotForAudioCallback() const noexcept {
    return activeSnapshot_;
}

std::size_t MixerController::pendingCommandCount() const noexcept {
    return commands_.pendingCount();
}

FixedMixerCommandQueue& MixerController::commandQueue() noexcept {
    return commands_;
}

MixerCommandResult MixerController::enqueue(MixerCommand command) noexcept {
    const auto validation = validate(command);
    if (!validation.ok()) {
        return validation;
    }
    return commands_.tryPushMixerCommand(command) ? MixerCommandResult::success() : MixerCommandResult::failure(MixerCommandError::QueueFull);
}

MixerCommandResult MixerController::enqueueFromMidi(const midi::MidiTargetCommand& command) noexcept {
    if (command.kind == midi::MidiTargetCommandKind::SetSequencerControl || command.kind == midi::MidiTargetCommandKind::TriggerLibraryAction) {
        return MixerCommandResult::failure(MixerCommandError::UnsupportedMidiCommand);
    }
    MixerCommand mixerCommand;
    mixerCommand.kind = mixerKindFromMidi(command);
    mixerCommand.deckIndex = command.targetIndex;
    mixerCommand.value = command.normalizedValue;
    mixerCommand.aux = command.parameterIndex;
    return enqueue(mixerCommand);
}

MixerCommandResult MixerController::processPendingUpdatesOutsideCallback(routing::AudioRoutingGraphController& routing) noexcept {
    MixerCommandResult result = MixerCommandResult::success();
    MixerCommand command;
    while (commands_.tryPopForMixerUpdate(command)) {
        const auto commandResult = apply(command, routing);
        if (!commandResult.ok()) {
            return commandResult;
        }
        result = commandResult;
    }
    return result;
}

void MixerController::publishDeckMeter(core::DeckId deckId, float left, float right) noexcept {
    if (!validDeckIndex(static_cast<std::uint32_t>(deckId.index()))) {
        return;
    }
    activeSnapshot_.decks[deckId.index()].meterLeft = clamp01(std::abs(left));
    activeSnapshot_.decks[deckId.index()].meterRight = clamp01(std::abs(right));
}

MixerCommandResult MixerController::validate(MixerCommand command) const noexcept {
    if (command.kind != MixerCommandKind::SetCrossfader && !validDeckIndex(command.deckIndex)) {
        return MixerCommandResult::failure(MixerCommandError::InvalidDeckId);
    }
    if (std::isnan(command.value)) {
        return MixerCommandResult::failure(MixerCommandError::InvalidValue);
    }
    if (command.kind == MixerCommandKind::TogglePluginBypass && command.aux >= routing::kPluginSlotsPerDeck) {
        return MixerCommandResult::failure(MixerCommandError::InvalidPluginSlot);
    }
    return MixerCommandResult::success();
}

MixerCommandResult MixerController::apply(MixerCommand command, routing::AudioRoutingGraphController& routing) noexcept {
    const auto validation = validate(command);
    if (!validation.ok()) {
        return validation;
    }

    auto next = activeSnapshot_;
    MixerDeckSnapshot* deck = command.kind == MixerCommandKind::SetCrossfader ? nullptr : &next.decks[command.deckIndex];
    switch (command.kind) {
    case MixerCommandKind::SetDeckVolume:
        deck->volume = clamp01(command.value);
        break;
    case MixerCommandKind::SetDeckGain:
        deck->gain = command.value < 0.0F ? 0.0F : std::min(command.value * 2.0F, 2.0F);
        break;
    case MixerCommandKind::SetDeckEqLow:
        deck->eqLow = clampEq(command.value * 2.0F);
        break;
    case MixerCommandKind::SetDeckEqMid:
        deck->eqMid = clampEq(command.value * 2.0F);
        break;
    case MixerCommandKind::SetDeckEqHigh:
        deck->eqHigh = clampEq(command.value * 2.0F);
        break;
    case MixerCommandKind::SetCrossfader:
        next.crossfader = clamp01(command.value);
        break;
    case MixerCommandKind::SetCueEnabled: {
        const auto deckId = core::DeckId::fromIndex(command.deckIndex).value;
        const auto cueEnabled = command.value >= 0.5F;
        const auto routed = routing.enqueueSetCueEnabled(deckId, cueEnabled);
        if (!routed.ok()) {
            return MixerCommandResult::routingFailure(routed.error);
        }
        deck->cueEnabled = cueEnabled;
        break;
    }
    case MixerCommandKind::SetDeckOutput: {
        const auto deckId = core::DeckId::fromIndex(command.deckIndex).value;
        const auto output = outputFromNormalizedValue(command.value);
        const auto routed = routing.enqueueAssignDeckOutput(deckId, output);
        if (!routed.ok()) {
            return MixerCommandResult::routingFailure(routed.error);
        }
        deck->output = output;
        break;
    }
    case MixerCommandKind::SetTransportPlay:
        deck->playing = command.value >= 0.5F;
        deck->transportTouched = true;
        break;
    case MixerCommandKind::SetTransportCue:
        deck->playing = false;
        deck->transportTouched = true;
        ++deck->cueEpoch;
        break;
    case MixerCommandKind::TogglePluginBypass:
        deck->pluginBypassed = command.value >= 0.5F;
        break;
    }

    ++next.revision;
    activeSnapshot_ = next;
    return MixerCommandResult::success();
}

float deckMainGain(const MixerSnapshot& snapshot, core::DeckId deckId) noexcept {
    if (!validDeckIndex(static_cast<std::uint32_t>(deckId.index()))) {
        return 0.0F;
    }
    const auto& deck = snapshot.decks[deckId.index()];
    float crossfaderGain = 1.0F;
    if (deckId.index() == 0U) {
        crossfaderGain = 1.0F - snapshot.crossfader;
    } else if (deckId.index() == 1U) {
        crossfaderGain = snapshot.crossfader;
    }
    return deck.volume * deck.gain * averageEq(deck) * crossfaderGain * snapshot.masterLevel;
}

float deckCueGain(const MixerSnapshot& snapshot, core::DeckId deckId) noexcept {
    if (!validDeckIndex(static_cast<std::uint32_t>(deckId.index()))) {
        return 0.0F;
    }
    return snapshot.decks[deckId.index()].cueEnabled ? snapshot.cueLevel : 0.0F;
}

const char* toString(MixerCommandError error) noexcept {
    switch (error) {
    case MixerCommandError::None:
        return "none";
    case MixerCommandError::InvalidDeckId:
        return "invalid-deck-id";
    case MixerCommandError::InvalidValue:
        return "invalid-value";
    case MixerCommandError::InvalidPluginSlot:
        return "invalid-plugin-slot";
    case MixerCommandError::QueueFull:
        return "queue-full";
    case MixerCommandError::UnsupportedMidiCommand:
        return "unsupported-midi-command";
    case MixerCommandError::RoutingRejected:
        return "routing-rejected";
    }
    return "invalid-value";
}

}
