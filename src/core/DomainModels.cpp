#include "core/DomainModels.h"

#include <cmath>
#include <utility>

namespace djapp::core {

namespace {

bool isPositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

bool isNonNegativeFinite(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool validDeckMainOutput(OutputBus output) {
    return output != OutputBus::Cue;
}

}

DomainResult<DeckId> DeckId::fromIndex(std::size_t deckIndex) {
    if (deckIndex >= 4) {
        return DomainResult<DeckId>::failure(DomainError::InvalidDeckId);
    }
    return DomainResult<DeckId>::success(DeckId(deckIndex));
}

std::array<DeckId, 4> allDeckIds() {
    return {DeckId::fromIndex(0).value, DeckId::fromIndex(1).value, DeckId::fromIndex(2).value, DeckId::fromIndex(3).value};
}

DomainResult<RoutingAssignment> RoutingAssignment::deckOutput(OutputBus mainOutput, bool cueEnabled) {
    if (!validDeckMainOutput(mainOutput)) {
        return DomainResult<RoutingAssignment>::failure(DomainError::InvalidRouting);
    }
    return DomainResult<RoutingAssignment>::success(RoutingAssignment{mainOutput, cueEnabled});
}

FourDeckCollection FourDeckCollection::createDefault() {
    FourDeckCollection collection;
    const auto ids = allDeckIds();
    for (std::size_t index = 0; index < collection.decks_.size(); ++index) {
        collection.decks_[index] = DeckState{ids[index], DeckType::AudioFile, RoutingAssignment{OutputBus::Master, false}, TransportState{}};
    }
    return collection;
}

DomainResult<DeckState*> FourDeckCollection::deck(DeckId id) {
    return deckByIndex(id.index());
}

DomainResult<const DeckState*> FourDeckCollection::deck(DeckId id) const {
    return deckByIndex(id.index());
}

DomainResult<DeckState*> FourDeckCollection::deckByIndex(std::size_t deckIndex) {
    if (deckIndex >= decks_.size()) {
        return DomainResult<DeckState*>::failure(DomainError::InvalidDeckId);
    }
    return DomainResult<DeckState*>::success(&decks_[deckIndex]);
}

DomainResult<const DeckState*> FourDeckCollection::deckByIndex(std::size_t deckIndex) const {
    if (deckIndex >= decks_.size()) {
        return DomainResult<const DeckState*>::failure(DomainError::InvalidDeckId);
    }
    return DomainResult<const DeckState*>::success(&decks_[deckIndex]);
}

UnitResult FourDeckCollection::setDeckType(DeckId id, DeckType type) {
    auto result = deck(id);
    if (!result.ok()) {
        return UnitResult::failure(result.error);
    }
    result.value->type = type;
    return UnitResult::success();
}

UnitResult FourDeckCollection::assignRouting(DeckId id, const RoutingAssignment& routing) {
    if (!validDeckMainOutput(routing.mainOutput)) {
        return UnitResult::failure(DomainError::InvalidRouting);
    }
    auto result = deck(id);
    if (!result.ok()) {
        return UnitResult::failure(result.error);
    }
    result.value->routing = routing;
    return UnitResult::success();
}

MasterClockState MasterClockState::stopped(double bpm) {
    MasterClockState clock;
    clock.setBpm(bpm);
    clock.playing = false;
    clock.positionBeats = 0.0;
    return clock;
}

UnitResult MasterClockState::setBpm(double nextBpm) {
    if (!isPositiveFinite(nextBpm)) {
        return UnitResult::failure(DomainError::InvalidBpm);
    }
    bpm = nextBpm;
    return UnitResult::success();
}

void MasterClockState::start() {
    playing = true;
}

void MasterClockState::stop() {
    playing = false;
}

UnitResult MasterClockState::advanceSeconds(double seconds) {
    if (!isNonNegativeFinite(seconds)) {
        return UnitResult::failure(DomainError::InvalidSeconds);
    }
    if (playing) {
        positionBeats += seconds * bpm / 60.0;
    }
    return UnitResult::success();
}

DomainResult<BeatgridMetadata> BeatgridMetadata::fromBpm(double bpm, double firstBeatSeconds) {
    if (!isPositiveFinite(bpm)) {
        return DomainResult<BeatgridMetadata>::failure(DomainError::InvalidBpm);
    }
    if (!isNonNegativeFinite(firstBeatSeconds)) {
        return DomainResult<BeatgridMetadata>::failure(DomainError::InvalidSeconds);
    }
    return DomainResult<BeatgridMetadata>::success(BeatgridMetadata{bpm, firstBeatSeconds});
}

DomainResult<TempoPitchSettings> TempoPitchSettings::fromValues(double sourceBpm,
                                                                double targetBpm,
                                                                bool tempoSyncEnabled,
                                                                bool pitchLockEnabled,
                                                                double pitchShiftCents,
                                                                bool bypass) {
    if (!isPositiveFinite(sourceBpm) || !isPositiveFinite(targetBpm) || !std::isfinite(pitchShiftCents)) {
        return DomainResult<TempoPitchSettings>::failure(DomainError::InvalidTempoPitchSettings);
    }
    return DomainResult<TempoPitchSettings>::success(TempoPitchSettings{sourceBpm, targetBpm, tempoSyncEnabled, pitchLockEnabled, pitchShiftCents, bypass});
}

double TempoPitchSettings::playbackRate() const noexcept {
    if (bypass || !tempoSyncEnabled || !isPositiveFinite(sourceBpm) || !isPositiveFinite(targetBpm)) {
        return 1.0;
    }
    return targetBpm / sourceBpm;
}

double TempoPitchSettings::effectiveTempoBpm() const noexcept {
    return sourceBpm * playbackRate();
}

double TempoPitchSettings::pitchDriftCents() const noexcept {
    if (bypass) {
        return 0.0;
    }
    if (pitchLockEnabled) {
        return pitchShiftCents;
    }
    return pitchShiftCents + (1200.0 * std::log2(playbackRate()));
}

MidiStepPattern MidiStepPattern::sixteenStepDefault(int note) {
    MidiStepPattern pattern;
    for (auto& step : pattern.steps) {
        step.note = note;
    }
    return pattern;
}

AnalysisJob AnalysisJob::queued(std::string id, std::string trackId) {
    return AnalysisJob{std::move(id), std::move(trackId), AnalysisJobStatus::Queued, 0.0};
}

UnitResult AnalysisJob::updateProgress(double nextProgress) {
    if (!std::isfinite(nextProgress) || nextProgress < 0.0 || nextProgress > 1.0) {
        return UnitResult::failure(DomainError::InvalidProgress);
    }
    progress = nextProgress;
    status = progress >= 1.0 ? AnalysisJobStatus::Complete : AnalysisJobStatus::Running;
    return UnitResult::success();
}

DomainResult<MidiLearnMapping> MidiLearnMapping::bind(MidiLearnTarget target, MidiMessageDescriptor message) {
    if (target.id.empty()) {
        return DomainResult<MidiLearnMapping>::failure(DomainError::InvalidIdentifier);
    }
    if (message.channel < 0 || message.channel > 15 || message.controller < 0 || message.controller > 127) {
        return DomainResult<MidiLearnMapping>::failure(DomainError::InvalidIdentifier);
    }
    return DomainResult<MidiLearnMapping>::success(MidiLearnMapping{std::move(target), message});
}

} 
