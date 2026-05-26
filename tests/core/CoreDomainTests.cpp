#include "core/DomainModels.h"

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

template <typename T>
int expectOk(const djapp::core::DomainResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int expectOk(const djapp::core::UnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

template <typename T>
int expectError(const djapp::core::DomainResult<T>& result, djapp::core::DomainError expected, const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == expected, message + " should return expected error");
}

int expectError(const djapp::core::UnitResult& result, djapp::core::DomainError expected, const std::string& message) {
    if (expect(!result.ok(), message + " should fail") != 0) {
        return 1;
    }
    return expect(result.error == expected, message + " should return expected error");
}

int testDecks() {
    using namespace djapp::core;

    const auto ids = allDeckIds();
    if (expect(ids.size() == 4, "domain should expose exactly four deck IDs") != 0) {
        return 1;
    }
    if (expect(ids[0].index() == 0 && ids[3].index() == 3, "deck IDs should be zero-based and ordered") != 0) {
        return 1;
    }

    if (expectOk(DeckId::fromIndex(3), "deck ID 3") != 0) {
        return 1;
    }
    if (expectError(DeckId::fromIndex(4), DomainError::InvalidDeckId, "deck ID 4") != 0) {
        return 1;
    }

    auto decks = FourDeckCollection::createDefault();
    if (expect(decks.size() == 4, "deck collection should contain exactly four slots") != 0) {
        return 1;
    }

    const auto deckTwo = DeckId::fromIndex(2).value;
    auto routingResult = RoutingAssignment::deckOutput(OutputBus::Output3, true);
    if (expectOk(routingResult, "valid deck routing") != 0) {
        return 1;
    }
    if (expectOk(decks.assignRouting(deckTwo, routingResult.value), "assign routing") != 0) {
        return 1;
    }
    if (expectOk(decks.setDeckType(deckTwo, DeckType::MidiStepSequencer), "switch deck type") != 0) {
        return 1;
    }

    const auto switched = decks.deck(deckTwo);
    if (expectOk(switched, "read switched deck") != 0) {
        return 1;
    }
    if (expect(switched.value->id == deckTwo, "type switch should preserve deck identity") != 0) {
        return 1;
    }
    if (expect(switched.value->type == DeckType::MidiStepSequencer, "type switch should update only deck type") != 0) {
        return 1;
    }
    if (expect(switched.value->routing.mainOutput == OutputBus::Output3 && switched.value->routing.cueEnabled,
               "type switch should preserve routing") != 0) {
        return 1;
    }
    if (expectError(decks.deckByIndex(8), DomainError::InvalidDeckId, "out-of-range deck access") != 0) {
        return 1;
    }

    return 0;
}

int testInvalidTransitions() {
    using namespace djapp::core;

    if (expectError(RoutingAssignment::deckOutput(OutputBus::Cue, false), DomainError::InvalidRouting, "cue as main deck output") != 0) {
        return 1;
    }

    auto clock = MasterClockState::stopped(120.0);
    if (expectError(clock.setBpm(0.0), DomainError::InvalidBpm, "zero BPM") != 0) {
        return 1;
    }

    auto job = AnalysisJob::queued("job-1", "track-1");
    if (expectError(job.updateProgress(1.2), DomainError::InvalidProgress, "analysis progress above one") != 0) {
        return 1;
    }

    auto learn = MidiLearnMapping::bind(MidiLearnTarget{"deck-1-filter", "Deck 1 Filter"}, MidiMessageDescriptor{0, 74});
    if (expectOk(learn, "valid MIDI Learn mapping") != 0) {
        return 1;
    }
    if (expectError(MidiLearnMapping::bind(MidiLearnTarget{"", "Missing"}, MidiMessageDescriptor{0, 74}),
                    DomainError::InvalidIdentifier,
                    "empty MIDI Learn target") != 0) {
        return 1;
    }

    return 0;
}

int testClock() {
    using namespace djapp::core;

    auto clock = MasterClockState::stopped(120.0);
    clock.start();
    if (expectOk(clock.advanceSeconds(0.5), "advance running clock") != 0) {
        return 1;
    }
    if (expect(std::abs(clock.positionBeats - 1.0) < 0.000001, "120 BPM should advance one beat in half a second") != 0) {
        return 1;
    }
    if (expectOk(clock.setBpm(60.0), "set deterministic BPM") != 0) {
        return 1;
    }
    if (expectOk(clock.advanceSeconds(2.0), "advance after BPM change") != 0) {
        return 1;
    }
    if (expect(std::abs(clock.positionBeats - 3.0) < 0.000001, "60 BPM should advance two beats in two seconds") != 0) {
        return 1;
    }
    clock.stop();
    if (expectOk(clock.advanceSeconds(10.0), "advance stopped clock") != 0) {
        return 1;
    }
    return expect(std::abs(clock.positionBeats - 3.0) < 0.000001, "stopped clock should not move");
}

int testDescriptors() {
    using namespace djapp::core;

    auto beatgrid = BeatgridMetadata::fromBpm(128.0, 0.125);
    if (expectOk(beatgrid, "beatgrid metadata") != 0) {
        return 1;
    }
    const LibraryTrack track{"track-1", "Title", "Artist", beatgrid.value, MusicalKey::Camelot8A};
    const Crate crate{"crate-1", "Peak Time", {track.id}};
    const Playlist playlist{"playlist-1", "Set", {track.id}};
    if (expect(crate.trackIds.size() == 1 && playlist.trackIds.size() == 1, "crate and playlist should retain track references") != 0) {
        return 1;
    }

    PluginChainDescriptor chain{"deck-1-fx", {PluginDescriptor{"eq", "EQ", true}, PluginDescriptor{"delay", "Delay", false}}};
    if (expect(chain.plugins.size() == 2 && chain.plugins[0].bypassed && !chain.plugins[1].bypassed,
               "plugin chain should describe ordered bypass states") != 0) {
        return 1;
    }

    auto pattern = MidiStepPattern::sixteenStepDefault(36);
    pattern.steps[0] = MidiStep{true, 36, 100, 0.25};
    if (expect(pattern.steps.size() == 16 && pattern.steps[0].enabled, "MIDI step pattern should expose sixteen steps") != 0) {
        return 1;
    }

    auto job = AnalysisJob::queued("job-1", track.id);
    if (expectOk(job.updateProgress(0.5), "analysis job progress") != 0) {
        return 1;
    }
    return expect(job.status == AnalysisJobStatus::Running && std::abs(job.progress - 0.5) < 0.000001,
                  "analysis job should record running progress");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "decks") {
        return testDecks();
    }
    if (filter == "invalid") {
        return testInvalidTransitions();
    }
    if (filter == "clock") {
        return testClock();
    }
    if (filter == "descriptors") {
        return testDescriptors();
    }

    if (testDecks() != 0 || testInvalidTransitions() != 0 || testClock() != 0 || testDescriptors() != 0) {
        return 1;
    }

    std::cout << "Core domain tests passed\n";
    return 0;
}
