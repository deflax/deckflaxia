#include "midi/MidiLearn.h"
#include "persistence/Persistence.h"

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

int expectOk(const djapp::persistence::PersistenceUnitResult& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

template <typename T>
int expectOk(const djapp::persistence::PersistenceResult<T>& result, const std::string& message) {
    return expect(result.ok(), message + " should succeed");
}

int testTargetRegistry() {
    using namespace djapp::midi;

    const auto registry = MidiLearnTargetRegistry::createAlphaDefault();
    if (expect(registry.find("deck.1.transport.play") != nullptr, "registry should include deck transport targets") != 0) {
        return 1;
    }
    if (expect(registry.find("mixer.deck.1.gain") != nullptr, "registry should include mixer targets") != 0) {
        return 1;
    }
    if (expect(registry.find("routing.deck.1.output") != nullptr, "registry should include routing targets") != 0) {
        return 1;
    }
    if (expect(registry.find("plugin.deck.1.slot.1.parameter.1") != nullptr, "registry should include plugin chain parameter targets") != 0) {
        return 1;
    }
    if (expect(registry.find("sequencer.deck.1.mute") != nullptr, "registry should include sequencer targets") != 0) {
        return 1;
    }
    if (expect(registry.find("library.action.load-selected") != nullptr, "registry should include library action targets") != 0) {
        return 1;
    }
    return expect(registry.size() >= 34, "registry should expose broad alpha target coverage");
}

int testPersistMapping() {
    using namespace djapp::core;
    using namespace djapp::midi;
    using namespace djapp::persistence;

    PersistenceService service;
    MidiLearnController controller;
    if (expect(controller.beginLearn("mixer.deck.1.gain"), "known target should enter learn mode") != 0) {
        return 1;
    }

    MidiMessageSignature signature{};
    if (expect(signatureFromMessage(MidiMessage::controlChange(0, 74, 100), signature), "CC should produce signature") != 0) {
        return 1;
    }
    if (expect(toStableString(signature) == "cc:ch=00:control=074", "CC signature should be deterministic") != 0) {
        return 1;
    }

    const auto learned = controller.capture(MidiMessage::controlChange(0, 74, 100));
    if (expect(learned.status == MidiLearnStatus::Learned && controller.mappingCount() == 1, "learn mode should bind one mapping") != 0) {
        return 1;
    }
    if (expectOk(service.midiMappings().save(learned.mapping), "persist learned MIDI mapping") != 0) {
        return 1;
    }

    MidiLearnController reloaded;
    const auto records = service.midiMappings().list();
    if (expectOk(records, "reload persisted MIDI mappings") != 0) {
        return 1;
    }
    reloaded.loadMappings(records.value);
    const auto dispatch = reloaded.dispatch(MidiMessage::controlChange(0, 74, 64));
    if (expect(dispatch.dispatched(), "reloaded mapping should dispatch") != 0) {
        return 1;
    }
    if (expect(dispatch.command.kind == MidiTargetCommandKind::SetDeckGain && dispatch.command.targetIndex == 0,
               "dispatch should produce mixer command intent") != 0) {
        return 1;
    }
    if (expect(std::abs(dispatch.command.normalizedValue - (64.0F / 127.0F)) < 0.0001F, "dispatch should normalize CC value") != 0) {
        return 1;
    }

    std::cout << "MidiLearn.PersistMapping target=" << dispatch.mapping.target.id << " signature=" << toStableString(signature) << '\n';
    return 0;
}

int testDuplicateConflict() {
    using namespace djapp::midi;

    MidiLearnController controller;
    const auto first = controller.bind("mixer.deck.1.gain", MidiMessage::controlChange(0, 10, 80));
    if (expect(first.status == MidiLearnStatus::Learned, "first mapping should learn") != 0) {
        return 1;
    }
    const auto duplicate = controller.bind("deck.1.transport.play", MidiMessage::controlChange(0, 10, 127));
    if (expect(duplicate.status == MidiLearnStatus::DuplicateConflict, "duplicate CC should return conflict") != 0) {
        return 1;
    }
    if (expect(duplicate.existingTargetId == "mixer.deck.1.gain" && duplicate.requestedTargetId == "deck.1.transport.play",
               "conflict should name existing and requested targets deterministically") != 0) {
        return 1;
    }
    if (expect(controller.mappingCount() == 1, "conflict should not mutate existing mappings") != 0) {
        return 1;
    }

    std::cout << "MidiLearn.DuplicateConflict existing=" << duplicate.existingTargetId << " requested=" << duplicate.requestedTargetId << '\n';
    return 0;
}

int testReconnectDispatch() {
    using namespace djapp::midi;

    MidiLearnController controller;
    const auto learned = controller.bind("routing.deck.1.output", MidiMessage::controlChange(2, 20, 127));
    if (expect(learned.status == MidiLearnStatus::Learned, "routing target should learn") != 0) {
        return 1;
    }
    controller.setDeviceConnected(false);
    const auto disconnected = controller.dispatch(MidiMessage::controlChange(2, 20, 64));
    if (expect(disconnected.status == MidiDispatchStatus::DeviceDisconnected, "disconnect should suppress dispatch") != 0) {
        return 1;
    }
    if (expect(controller.mappingCount() == 1, "disconnect should not clear learned mappings") != 0) {
        return 1;
    }
    controller.setDeviceConnected(true);
    const auto dispatch = controller.dispatch(MidiMessage::controlChange(2, 20, 64));
    if (expect(dispatch.dispatched(), "reconnect should restore dispatch without relearn") != 0) {
        return 1;
    }
    return expect(dispatch.command.kind == MidiTargetCommandKind::SetDeckOutput, "routing dispatch should produce safe command intent");
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";

    if (filter == "registry") {
        return testTargetRegistry();
    }
    if (filter == "persist") {
        return testPersistMapping();
    }
    if (filter == "conflict") {
        return testDuplicateConflict();
    }
    if (filter == "reconnect") {
        return testReconnectDispatch();
    }

    if (testTargetRegistry() != 0 || testPersistMapping() != 0 || testDuplicateConflict() != 0 || testReconnectDispatch() != 0) {
        return 1;
    }

    std::cout << "MIDI Learn tests passed\n";
    return 0;
}
