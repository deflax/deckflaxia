#include "midi/MidiLearn.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace djapp::midi {

namespace {

void addTarget(MidiLearnTargetRegistry& registry,
               std::string id,
               std::string displayName,
               MidiLearnTargetKind kind,
               MidiTargetCommandKind commandKind,
               std::uint32_t targetIndex,
               std::uint32_t parameterIndex = 0) {
    (void)registry.registerTarget(MidiLearnTargetDefinition{core::MidiLearnTarget{std::move(id), std::move(displayName)},
                                                           kind,
                                                           commandKind,
                                                           targetIndex,
                                                           parameterIndex});
}

bool sameSignature(const core::MidiMessageDescriptor& descriptor, MidiMessageSignature signature) noexcept {
    return descriptor.channel == signature.channel && descriptor.controller == signature.control;
}

float valueFromMessage(const MidiMessage& message) noexcept {
    return static_cast<float>(message.value()) / 127.0F;
}

MidiTargetCommand commandFromTarget(const MidiLearnTargetDefinition& target, const MidiMessage& message) noexcept {
    return MidiTargetCommand{target.commandKind, target.targetIndex, target.parameterIndex, valueFromMessage(message)};
}

}

MidiLearnTargetRegistry MidiLearnTargetRegistry::createAlphaDefault() {
    MidiLearnTargetRegistry registry;
    for (std::uint32_t deck = 0; deck < 4; ++deck) {
        const auto deckLabel = std::to_string(deck + 1U);
        addTarget(registry, "deck." + deckLabel + ".transport.play", "Deck " + deckLabel + " Play", MidiLearnTargetKind::DeckTransport, MidiTargetCommandKind::SetDeckTransport, deck, 1);
        addTarget(registry, "deck." + deckLabel + ".transport.cue", "Deck " + deckLabel + " Cue", MidiLearnTargetKind::DeckTransport, MidiTargetCommandKind::SetDeckTransport, deck, 2);
        addTarget(registry, "mixer.deck." + deckLabel + ".gain", "Deck " + deckLabel + " Gain", MidiLearnTargetKind::MixerRouting, MidiTargetCommandKind::SetDeckGain, deck);
        addTarget(registry, "routing.deck." + deckLabel + ".output", "Deck " + deckLabel + " Output", MidiLearnTargetKind::MixerRouting, MidiTargetCommandKind::SetDeckOutput, deck);
        addTarget(registry, "routing.deck." + deckLabel + ".cue", "Deck " + deckLabel + " Cue Send", MidiLearnTargetKind::MixerRouting, MidiTargetCommandKind::SetDeckOutput, deck, 1);
        addTarget(registry, "sequencer.deck." + deckLabel + ".mute", "Sequencer Deck " + deckLabel + " Mute", MidiLearnTargetKind::SequencerControl, MidiTargetCommandKind::SetSequencerControl, deck);
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            addTarget(registry,
                      "plugin.deck." + deckLabel + ".slot." + std::to_string(slot + 1U) + ".parameter.1",
                      "Deck " + deckLabel + " Plugin Slot " + std::to_string(slot + 1U) + " Parameter 1",
                      MidiLearnTargetKind::PluginChainParameter,
                      MidiTargetCommandKind::SetPluginParameter,
                      deck,
                      slot);
        }
    }
    addTarget(registry, "library.action.load-selected", "Library Load Selected", MidiLearnTargetKind::LibraryAction, MidiTargetCommandKind::TriggerLibraryAction, 0);
    addTarget(registry, "library.action.focus-search", "Library Focus Search", MidiLearnTargetKind::LibraryAction, MidiTargetCommandKind::TriggerLibraryAction, 1);
    return registry;
}

bool MidiLearnTargetRegistry::registerTarget(MidiLearnTargetDefinition definition) {
    if (definition.target.id.empty() || find(definition.target.id) != nullptr) {
        return false;
    }
    targets_.push_back(std::move(definition));
    return true;
}

const MidiLearnTargetDefinition* MidiLearnTargetRegistry::find(const std::string& targetId) const noexcept {
    const auto found = std::find_if(targets_.begin(), targets_.end(), [&](const MidiLearnTargetDefinition& definition) {
        return definition.target.id == targetId;
    });
    return found == targets_.end() ? nullptr : &(*found);
}

std::size_t MidiLearnTargetRegistry::size() const noexcept {
    return targets_.size();
}

const std::vector<MidiLearnTargetDefinition>& MidiLearnTargetRegistry::targets() const noexcept {
    return targets_;
}

MidiLearnController::MidiLearnController(MidiLearnTargetRegistry registry) : registry_(std::move(registry)) {}

const MidiLearnTargetRegistry& MidiLearnController::registry() const noexcept {
    return registry_;
}

std::size_t MidiLearnController::mappingCount() const noexcept {
    return mappings_.size();
}

const std::vector<core::MidiLearnMapping>& MidiLearnController::mappings() const noexcept {
    return mappings_;
}

bool MidiLearnController::deviceConnected() const noexcept {
    return deviceConnected_;
}

void MidiLearnController::setDeviceConnected(bool connected) noexcept {
    deviceConnected_ = connected;
}

bool MidiLearnController::beginLearn(std::string targetId) {
    if (registry_.find(targetId) == nullptr) {
        learning_ = false;
        learningTargetId_.clear();
        return false;
    }
    learningTargetId_ = std::move(targetId);
    learning_ = true;
    return true;
}

void MidiLearnController::cancelLearn() noexcept {
    learning_ = false;
    learningTargetId_.clear();
}

bool MidiLearnController::learning() const noexcept {
    return learning_;
}

MidiLearnResult MidiLearnController::capture(const MidiMessage& message) {
    if (!learning_) {
        return MidiLearnResult{MidiLearnStatus::NotLearning, {}, {}, {}};
    }
    MidiMessageSignature signature{};
    if (!signatureFromMessage(message, signature)) {
        return MidiLearnResult{MidiLearnStatus::UnsupportedMessage, {}, {}, learningTargetId_};
    }
    auto result = bindSignature(learningTargetId_, signature);
    if (result.ok()) {
        cancelLearn();
    }
    return result;
}

MidiLearnResult MidiLearnController::bind(std::string targetId, const MidiMessage& message) {
    MidiMessageSignature signature{};
    if (!signatureFromMessage(message, signature)) {
        return MidiLearnResult{MidiLearnStatus::UnsupportedMessage, {}, {}, std::move(targetId)};
    }
    return bindSignature(std::move(targetId), signature);
}

MidiDispatchResult MidiLearnController::dispatch(const MidiMessage& message) const {
    if (!deviceConnected_) {
        return MidiDispatchResult{MidiDispatchStatus::DeviceDisconnected, {}, {}};
    }
    MidiMessageSignature signature{};
    if (!signatureFromMessage(message, signature)) {
        return MidiDispatchResult{MidiDispatchStatus::UnsupportedMessage, {}, {}};
    }
    const auto mapping = std::find_if(mappings_.begin(), mappings_.end(), [&](const core::MidiLearnMapping& candidate) {
        return sameSignature(candidate.message, signature);
    });
    if (mapping == mappings_.end()) {
        return MidiDispatchResult{MidiDispatchStatus::NoMapping, {}, {}};
    }
    const auto* target = registry_.find(mapping->target.id);
    if (target == nullptr) {
        return MidiDispatchResult{MidiDispatchStatus::InvalidTarget, {}, *mapping};
    }
    return MidiDispatchResult{MidiDispatchStatus::Dispatched, commandFromTarget(*target, message), *mapping};
}

void MidiLearnController::loadMappings(std::vector<core::MidiLearnMapping> mappings) {
    mappings_.clear();
    for (auto& mapping : mappings) {
        if (registry_.find(mapping.target.id) != nullptr) {
            mappings_.push_back(std::move(mapping));
        }
    }
}

MidiLearnResult MidiLearnController::bindSignature(std::string targetId, MidiMessageSignature signature) {
    const auto* target = registry_.find(targetId);
    if (target == nullptr) {
        return MidiLearnResult{MidiLearnStatus::InvalidTarget, {}, {}, std::move(targetId)};
    }

    const auto duplicate = std::find_if(mappings_.begin(), mappings_.end(), [&](const core::MidiLearnMapping& mapping) {
        return sameSignature(mapping.message, signature);
    });
    if (duplicate != mappings_.end() && duplicate->target.id != targetId) {
        return MidiLearnResult{MidiLearnStatus::DuplicateConflict, {}, duplicate->target.id, std::move(targetId)};
    }

    const auto mappingResult = core::MidiLearnMapping::bind(target->target, descriptorFromSignature(signature));
    if (!mappingResult.ok()) {
        return MidiLearnResult{MidiLearnStatus::UnsupportedMessage, {}, {}, std::move(targetId)};
    }

    auto targetMapping = std::find_if(mappings_.begin(), mappings_.end(), [&](const core::MidiLearnMapping& mapping) {
        return mapping.target.id == targetId;
    });
    if (targetMapping != mappings_.end()) {
        *targetMapping = mappingResult.value;
        return MidiLearnResult{duplicate == mappings_.end() ? MidiLearnStatus::Learned : MidiLearnStatus::AlreadyMapped,
                               mappingResult.value,
                               {},
                               targetId};
    }

    mappings_.push_back(mappingResult.value);
    return MidiLearnResult{MidiLearnStatus::Learned, mappingResult.value, {}, targetId};
}

bool signatureFromMessage(const MidiMessage& message, MidiMessageSignature& signature) noexcept {
    if (message.channel > 15U) {
        return false;
    }
    switch (message.kind) {
    case MidiMessageKind::ControlChange:
        if (message.controller() > 127U) {
            return false;
        }
        signature = MidiMessageSignature{MidiMessageKind::ControlChange, message.channel, message.controller()};
        return true;
    case MidiMessageKind::NoteOn:
    case MidiMessageKind::NoteOff:
        return false;
    }
    return false;
}

std::string toStableString(MidiMessageSignature signature) {
    std::ostringstream stream;
    stream << toString(signature.kind) << ":ch=" << std::setw(2) << std::setfill('0') << static_cast<int>(signature.channel)
           << ":control=" << std::setw(3) << std::setfill('0') << static_cast<int>(signature.control);
    return stream.str();
}

core::MidiMessageDescriptor descriptorFromSignature(MidiMessageSignature signature) noexcept {
    return core::MidiMessageDescriptor{static_cast<int>(signature.channel), static_cast<int>(signature.control)};
}

MidiMessageSignature signatureFromDescriptor(core::MidiMessageDescriptor descriptor) noexcept {
    return MidiMessageSignature{MidiMessageKind::ControlChange, static_cast<std::uint8_t>(descriptor.channel), static_cast<std::uint8_t>(descriptor.controller)};
}

}
