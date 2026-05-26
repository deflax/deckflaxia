#pragma once

#include "core/DomainModels.h"
#include "midi/MidiBuffer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace djapp::midi {

enum class MidiLearnTargetKind : std::uint8_t {
    DeckTransport,
    MixerRouting,
    PluginChainParameter,
    SequencerControl,
    LibraryAction,
};

enum class MidiTargetCommandKind : std::uint8_t {
    SetDeckTransport,
    SetDeckGain,
    SetDeckOutput,
    SetPluginParameter,
    SetSequencerControl,
    TriggerLibraryAction,
};

enum class MidiLearnStatus : std::uint8_t {
    Learned,
    AlreadyMapped,
    DuplicateConflict,
    InvalidTarget,
    NotLearning,
    UnsupportedMessage,
};

enum class MidiDispatchStatus : std::uint8_t {
    Dispatched,
    NoMapping,
    InvalidTarget,
    UnsupportedMessage,
    DeviceDisconnected,
};

struct MidiMessageSignature final {
    MidiMessageKind kind{MidiMessageKind::ControlChange};
    std::uint8_t channel{};
    std::uint8_t control{};

    friend bool operator==(MidiMessageSignature left, MidiMessageSignature right) noexcept {
        return left.kind == right.kind && left.channel == right.channel && left.control == right.control;
    }
};

struct MidiLearnTargetDefinition final {
    core::MidiLearnTarget target;
    MidiLearnTargetKind kind{MidiLearnTargetKind::DeckTransport};
    MidiTargetCommandKind commandKind{MidiTargetCommandKind::SetDeckTransport};
    std::uint32_t targetIndex{};
    std::uint32_t parameterIndex{};
};

struct MidiTargetCommand final {
    MidiTargetCommandKind kind{MidiTargetCommandKind::SetDeckTransport};
    std::uint32_t targetIndex{};
    std::uint32_t parameterIndex{};
    float normalizedValue{};
};

struct MidiLearnResult final {
    MidiLearnStatus status{MidiLearnStatus::NotLearning};
    core::MidiLearnMapping mapping{};
    std::string existingTargetId;
    std::string requestedTargetId;

    [[nodiscard]] bool ok() const noexcept {
        return status == MidiLearnStatus::Learned || status == MidiLearnStatus::AlreadyMapped;
    }
};

struct MidiDispatchResult final {
    MidiDispatchStatus status{MidiDispatchStatus::NoMapping};
    MidiTargetCommand command{};
    core::MidiLearnMapping mapping{};

    [[nodiscard]] bool dispatched() const noexcept { return status == MidiDispatchStatus::Dispatched; }
};

class MidiLearnTargetRegistry final {
public:
    static MidiLearnTargetRegistry createAlphaDefault();

    [[nodiscard]] bool registerTarget(MidiLearnTargetDefinition definition);
    [[nodiscard]] const MidiLearnTargetDefinition* find(const std::string& targetId) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<MidiLearnTargetDefinition>& targets() const noexcept;

private:
    std::vector<MidiLearnTargetDefinition> targets_;
};

class MidiLearnController final {
public:
    explicit MidiLearnController(MidiLearnTargetRegistry registry = MidiLearnTargetRegistry::createAlphaDefault());

    [[nodiscard]] const MidiLearnTargetRegistry& registry() const noexcept;
    [[nodiscard]] std::size_t mappingCount() const noexcept;
    [[nodiscard]] const std::vector<core::MidiLearnMapping>& mappings() const noexcept;
    [[nodiscard]] bool deviceConnected() const noexcept;
    void setDeviceConnected(bool connected) noexcept;

    [[nodiscard]] bool beginLearn(std::string targetId);
    void cancelLearn() noexcept;
    [[nodiscard]] bool learning() const noexcept;
    [[nodiscard]] MidiLearnResult capture(const MidiMessage& message);
    [[nodiscard]] MidiLearnResult bind(std::string targetId, const MidiMessage& message);
    [[nodiscard]] MidiDispatchResult dispatch(const MidiMessage& message) const;
    void loadMappings(std::vector<core::MidiLearnMapping> mappings);

private:
    [[nodiscard]] MidiLearnResult bindSignature(std::string targetId, MidiMessageSignature signature);

    MidiLearnTargetRegistry registry_;
    std::vector<core::MidiLearnMapping> mappings_;
    std::string learningTargetId_;
    bool learning_{};
    bool deviceConnected_{true};
};

[[nodiscard]] bool signatureFromMessage(const MidiMessage& message, MidiMessageSignature& signature) noexcept;
[[nodiscard]] std::string toStableString(MidiMessageSignature signature);
[[nodiscard]] core::MidiMessageDescriptor descriptorFromSignature(MidiMessageSignature signature) noexcept;
[[nodiscard]] MidiMessageSignature signatureFromDescriptor(core::MidiMessageDescriptor descriptor) noexcept;

}
