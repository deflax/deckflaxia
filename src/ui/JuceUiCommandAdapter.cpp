#include "ui/JuceUiCommandAdapter.h"

#include "ui/VST3EditorUi.h"

#include <filesystem>
#include <utility>

namespace deckflaxia::ui {
namespace {

[[nodiscard]] JuceUiCommandResult result(JuceUiCommandStatus status,
                                         JuceUiCommandDomain domain,
                                         const char* action,
                                         std::string detail) {
    return JuceUiCommandResult{status, domain, action, std::move(detail)};
}

[[nodiscard]] JuceUiCommandResult success(JuceUiCommandDomain domain, const char* action, const char* detail) {
    return result(JuceUiCommandStatus::Succeeded, domain, action, detail);
}

[[nodiscard]] JuceUiCommandResult invalidDeck(JuceUiCommandDomain domain, const char* action) {
    return result(JuceUiCommandStatus::InvalidArgument, domain, action, "invalid-deck-id");
}

[[nodiscard]] core::DomainResult<core::DeckId> deckIdFromIndex(std::size_t deckIndex) {
    return core::DeckId::fromIndex(deckIndex);
}

[[nodiscard]] std::uint32_t normalizedOutputValue(core::OutputBus output) noexcept {
    switch (output) {
    case core::OutputBus::Master:
        return 0U;
    case core::OutputBus::Output1:
        return 1U;
    case core::OutputBus::Output2:
        return 2U;
    case core::OutputBus::Output3:
        return 3U;
    case core::OutputBus::Output4:
        return 4U;
    case core::OutputBus::Cue:
        return 5U;
    }
    return 5U;
}

[[nodiscard]] float mixerOutputValue(core::OutputBus output) noexcept {
    switch (normalizedOutputValue(output)) {
    case 0U:
        return 0.0F;
    case 1U:
        return 0.25F;
    case 2U:
        return 0.45F;
    case 3U:
        return 0.65F;
    case 4U:
        return 0.85F;
    default:
        return 1.0F;
    }
}

[[nodiscard]] JuceUiCommandResult fromMixerResult(audio::MixerCommandResult commandResult, const char* action) {
    if (commandResult.ok()) {
        return success(JuceUiCommandDomain::Mixer, action, "mixer-command-applied");
    }
    if (commandResult.error == audio::MixerCommandError::InvalidDeckId ||
        commandResult.error == audio::MixerCommandError::InvalidValue ||
        commandResult.error == audio::MixerCommandError::InvalidPluginSlot) {
        return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::Mixer, action, audio::toString(commandResult.error));
    }
    return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::Mixer, action, audio::toString(commandResult.error));
}

[[nodiscard]] JuceUiCommandResult fromDeckResult(decks::FourDeckPlaybackError error, const char* action) {
    if (error == decks::FourDeckPlaybackError::None) {
        return success(JuceUiCommandDomain::DeckTransport, action, "deck-command-applied");
    }
    if (error == decks::FourDeckPlaybackError::InvalidDeckId) {
        return invalidDeck(JuceUiCommandDomain::DeckTransport, action);
    }
    return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::DeckTransport, action, decks::toString(error));
}

[[nodiscard]] JuceUiCommandResult fromPluginResult(plugins::PluginHostResult commandResult, const char* action) {
    if (commandResult.ok()) {
        return success(JuceUiCommandDomain::PluginChain, action, "plugin-host-command-applied");
    }
    if (commandResult.error == plugins::PluginHostError::InvalidSlot || commandResult.error == plugins::PluginHostError::InvalidParameter) {
        return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::PluginChain, action, plugins::toString(commandResult.error));
    }
    if (commandResult.error == plugins::PluginHostError::HostUnavailable || commandResult.error == plugins::PluginHostError::PluginUnavailable) {
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::PluginChain, action, plugins::toString(commandResult.error));
    }
    return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::PluginChain, action, plugins::toString(commandResult.error));
}

[[nodiscard]] std::size_t toSlotForMoveUp(std::size_t slotIndex) noexcept {
    return slotIndex == 0U ? 0U : slotIndex - 1U;
}

}

JuceUiCommandAdapter::JuceUiCommandAdapter(JuceUiCommandAdapterServices services) noexcept : services_(services) {}

JuceUiCommandResult JuceUiCommandAdapter::dispatch(const DeckTransportIntent& intent) noexcept {
    if (services_.playbackCore != nullptr) {
        return dispatchToPlaybackCore(intent);
    }
    if (services_.mixer != nullptr) {
        return dispatchToMixer(intent);
    }
    return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::DeckTransport, toString(intent.action), "deck-transport-backend-unavailable");
}

JuceUiCommandResult JuceUiCommandAdapter::dispatchToPlaybackCore(const DeckTransportIntent& intent) noexcept {
    const auto deckId = deckIdFromIndex(intent.deckIndex);
    if (!deckId.ok()) {
        return invalidDeck(JuceUiCommandDomain::DeckTransport, toString(intent.action));
    }
    if (!services_.playbackCore->deck(deckId.value).state().loaded) {
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::DeckTransport, toString(intent.action), "deck-not-loaded");
    }

    switch (intent.action) {
    case DeckTransportAction::Play:
        return fromDeckResult(services_.playbackCore->play(deckId.value), toString(intent.action));
    case DeckTransportAction::Pause:
        return fromDeckResult(services_.playbackCore->pause(deckId.value), toString(intent.action));
    case DeckTransportAction::Stop:
        return fromDeckResult(services_.playbackCore->stop(deckId.value), toString(intent.action));
    case DeckTransportAction::Cue:
        return fromDeckResult(services_.playbackCore->cue(deckId.value), toString(intent.action));
    case DeckTransportAction::Sync:
        return fromDeckResult(services_.playbackCore->syncTempo(deckId.value, intent.sourceBpm, intent.targetBpm, intent.pitchLockEnabled), toString(intent.action));
    }
    return result(JuceUiCommandStatus::Unsupported, JuceUiCommandDomain::DeckTransport, toString(intent.action), "deck-transport-action-unsupported");
}

JuceUiCommandResult JuceUiCommandAdapter::dispatchToMixer(const DeckTransportIntent& intent) noexcept {
    audio::MixerCommand command;
    command.deckIndex = static_cast<std::uint32_t>(intent.deckIndex);
    command.value = intent.action == DeckTransportAction::Play ? 1.0F : 0.0F;
    switch (intent.action) {
    case DeckTransportAction::Play:
    case DeckTransportAction::Pause:
    case DeckTransportAction::Stop:
        command.kind = audio::MixerCommandKind::SetTransportPlay;
        break;
    case DeckTransportAction::Cue:
        command.kind = audio::MixerCommandKind::SetTransportCue;
        break;
    case DeckTransportAction::Sync:
        return result(JuceUiCommandStatus::Unsupported, JuceUiCommandDomain::DeckTransport, toString(intent.action), "tempo-sync-requires-playback-core");
    }
    auto commandResult = services_.mixer->enqueue(command);
    if (commandResult.ok() && services_.routing != nullptr) {
        commandResult = services_.mixer->processPendingUpdatesOutsideCallback(*services_.routing);
    }
    if (commandResult.ok()) {
        return success(JuceUiCommandDomain::DeckTransport, toString(intent.action), services_.routing != nullptr ? "mixer-transport-command-applied" : "mixer-transport-command-enqueued");
    }
    if (commandResult.error == audio::MixerCommandError::InvalidDeckId) {
        return invalidDeck(JuceUiCommandDomain::DeckTransport, toString(intent.action));
    }
    return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::DeckTransport, toString(intent.action), audio::toString(commandResult.error));
}

JuceUiCommandResult JuceUiCommandAdapter::dispatch(const MixerIntent& intent) noexcept {
    if (services_.mixer == nullptr) {
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::Mixer, toString(intent.action), "mixer-backend-unavailable");
    }

    audio::MixerCommand command;
    command.deckIndex = static_cast<std::uint32_t>(intent.deckIndex);
    command.value = intent.value;
    switch (intent.action) {
    case MixerAction::Volume:
        command.kind = audio::MixerCommandKind::SetDeckVolume;
        break;
    case MixerAction::Gain:
        command.kind = audio::MixerCommandKind::SetDeckGain;
        break;
    case MixerAction::EqLow:
        command.kind = audio::MixerCommandKind::SetDeckEqLow;
        break;
    case MixerAction::EqMid:
        command.kind = audio::MixerCommandKind::SetDeckEqMid;
        break;
    case MixerAction::EqHigh:
        command.kind = audio::MixerCommandKind::SetDeckEqHigh;
        break;
    case MixerAction::Crossfader:
        command.kind = audio::MixerCommandKind::SetCrossfader;
        command.deckIndex = 0;
        break;
    case MixerAction::Cue:
        command.kind = audio::MixerCommandKind::SetCueEnabled;
        command.value = intent.enabled ? 1.0F : 0.0F;
        break;
    case MixerAction::Output:
        if (intent.output == core::OutputBus::Cue) {
            return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::Mixer, toString(intent.action), "cue-is-not-main-output");
        }
        command.kind = audio::MixerCommandKind::SetDeckOutput;
        command.value = mixerOutputValue(intent.output);
        break;
    }

    auto commandResult = services_.mixer->enqueue(command);
    if (commandResult.ok() && services_.routing != nullptr) {
        commandResult = services_.mixer->processPendingUpdatesOutsideCallback(*services_.routing);
    }
    return fromMixerResult(commandResult, toString(intent.action));
}

JuceUiCommandResult JuceUiCommandAdapter::dispatch(const BrowserIntent& intent) {
    switch (intent.action) {
    case BrowserAction::Import: {
        const auto classification = library::classifyAudioImport(intent.entry);
        if (services_.browserRows != nullptr) {
            services_.browserRows->push_back(classification);
        }
        if (!classification.importable()) {
            return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::Browser, toString(intent.action), library::toString(classification.error));
        }
        return success(JuceUiCommandDomain::Browser, toString(intent.action), "audio-import-classified");
    }
    case BrowserAction::LoadToDeck:
        if (services_.playbackCore == nullptr) {
            return result(JuceUiCommandStatus::Unsupported, JuceUiCommandDomain::Browser, toString(intent.action), "browser-load-runtime-selection-not-wired");
        }
        if (const auto deckId = deckIdFromIndex(intent.deckIndex); !deckId.ok()) {
            return invalidDeck(JuceUiCommandDomain::Browser, toString(intent.action));
        } else {
            const auto classification = library::classifyAudioImport(intent.entry);
            if (!classification.importable()) {
                return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::Browser, toString(intent.action), library::toString(classification.error));
            }
            decks::PreparedAudioMedia media;
            const auto loaded = decks::loadPcm16WavFileToPreparedMedia(std::filesystem::path{intent.entry.path}, media);
            if (!loaded.ok()) {
                return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::Browser, toString(intent.action), decks::toString(loaded.error));
            }
            const auto deckLoaded = services_.playbackCore->loadDeck(deckId.value, decks::AudioDeckMediaReference::preparedAudio(std::move(media)));
            if (!deckLoaded.ok()) {
                return result(JuceUiCommandStatus::BackendRejected, JuceUiCommandDomain::Browser, toString(intent.action), decks::toString(deckLoaded.error));
            }
            return success(JuceUiCommandDomain::Browser, toString(intent.action), "browser-track-loaded-to-deck");
        }
    case BrowserAction::SelectRow:
        if (services_.browserRows == nullptr) {
            return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::Browser, toString(intent.action), "browser-model-unavailable");
        }
        if (intent.rowIndex >= services_.browserRows->size()) {
            return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::Browser, toString(intent.action), "invalid-browser-row");
        }
        return success(JuceUiCommandDomain::Browser, toString(intent.action), "browser-row-selected");
    }
    return result(JuceUiCommandStatus::Unsupported, JuceUiCommandDomain::Browser, toString(intent.action), "browser-action-unsupported");
}

JuceUiCommandResult JuceUiCommandAdapter::dispatch(const PluginChainIntent& intent) noexcept {
    if (intent.action == PluginChainAction::OpenEditor || intent.action == PluginChainAction::CloseEditor || intent.action == PluginChainAction::Bypass ||
        intent.action == PluginChainAction::Parameter) {
        if (services_.pluginHost != nullptr) {
            return dispatchHostPluginIntent(intent);
        }
    }
    if (services_.pluginDescriptor != nullptr) {
        return dispatchDescriptorPluginIntent(intent);
    }
    return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::PluginChain, toString(intent.action), "plugin-chain-backend-unavailable");
}

JuceUiCommandResult JuceUiCommandAdapter::dispatchHostPluginIntent(const PluginChainIntent& intent) noexcept {
    switch (intent.action) {
    case PluginChainAction::Bypass:
        return fromPluginResult(services_.pluginHost->setSlotBypass(intent.slotIndex, intent.bypassed), toString(intent.action));
    case PluginChainAction::Parameter:
        return fromPluginResult(services_.pluginHost->setParameter(intent.slotIndex, intent.parameterId, intent.normalizedValue), toString(intent.action));
    case PluginChainAction::OpenEditor: {
        const auto status = services_.pluginHost->openSeparateEditorWindow(intent.slotIndex);
        if (status.statusText == "invalid-slot") {
            return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::PluginChain, toString(intent.action), status.statusText);
        }
        if (status.open || status.genericParameterSurfaceAvailable) {
            return result(JuceUiCommandStatus::Succeeded, JuceUiCommandDomain::PluginChain, toString(intent.action), status.statusText);
        }
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::PluginChain, toString(intent.action), status.statusText);
    }
    case PluginChainAction::CloseEditor: {
        const auto status = services_.pluginHost->closeSeparateEditorWindow(intent.slotIndex);
        if (status.statusText == "invalid-slot") {
            return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::PluginChain, toString(intent.action), status.statusText);
        }
        return result(JuceUiCommandStatus::Succeeded, JuceUiCommandDomain::PluginChain, toString(intent.action), status.statusText);
    }
    case PluginChainAction::Remove:
    case PluginChainAction::MoveUp:
    case PluginChainAction::MoveDown:
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::PluginChain, toString(intent.action), "runtime-plugin-chain-reorder-unavailable");
    }
    return result(JuceUiCommandStatus::Unsupported, JuceUiCommandDomain::PluginChain, toString(intent.action), "plugin-action-unsupported");
}

JuceUiCommandResult JuceUiCommandAdapter::dispatchDescriptorPluginIntent(const PluginChainIntent& intent) noexcept {
    bool applied = false;
    switch (intent.action) {
    case PluginChainAction::Bypass:
        applied = setPluginSlotBypass(*services_.pluginDescriptor, intent.slotIndex, intent.bypassed);
        break;
    case PluginChainAction::Remove:
        applied = removePluginSlot(*services_.pluginDescriptor, intent.slotIndex);
        break;
    case PluginChainAction::MoveUp:
        applied = intent.slotIndex > 0U && movePluginSlot(*services_.pluginDescriptor, intent.slotIndex, toSlotForMoveUp(intent.slotIndex));
        break;
    case PluginChainAction::MoveDown:
        applied = intent.slotIndex + 1U < services_.pluginDescriptor->plugins.size() && movePluginSlot(*services_.pluginDescriptor, intent.slotIndex, intent.slotIndex + 1U);
        break;
    case PluginChainAction::Parameter:
        applied = setPluginParameter(*services_.pluginDescriptor, intent.slotIndex, intent.parameterId, intent.normalizedValue);
        break;
    case PluginChainAction::OpenEditor:
    case PluginChainAction::CloseEditor:
        return result(JuceUiCommandStatus::Unavailable, JuceUiCommandDomain::PluginChain, toString(intent.action), "plugin-editor-host-unavailable");
    }
    if (applied) {
        return success(JuceUiCommandDomain::PluginChain, toString(intent.action), "plugin-descriptor-command-applied");
    }
    return result(JuceUiCommandStatus::InvalidArgument, JuceUiCommandDomain::PluginChain, toString(intent.action), "invalid-plugin-slot-or-parameter");
}

const char* toString(JuceUiCommandStatus status) noexcept {
    switch (status) {
    case JuceUiCommandStatus::Succeeded:
        return "succeeded";
    case JuceUiCommandStatus::Unsupported:
        return "unsupported";
    case JuceUiCommandStatus::Unavailable:
        return "unavailable";
    case JuceUiCommandStatus::InvalidArgument:
        return "invalid-argument";
    case JuceUiCommandStatus::BackendRejected:
        return "backend-rejected";
    }
    return "backend-rejected";
}

const char* toString(JuceUiCommandDomain domain) noexcept {
    switch (domain) {
    case JuceUiCommandDomain::DeckTransport:
        return "deck-transport";
    case JuceUiCommandDomain::Mixer:
        return "mixer";
    case JuceUiCommandDomain::Browser:
        return "browser";
    case JuceUiCommandDomain::PluginChain:
        return "plugin-chain";
    }
    return "plugin-chain";
}

const char* toString(DeckTransportAction action) noexcept {
    switch (action) {
    case DeckTransportAction::Play:
        return "play";
    case DeckTransportAction::Pause:
        return "pause";
    case DeckTransportAction::Stop:
        return "stop";
    case DeckTransportAction::Cue:
        return "cue";
    case DeckTransportAction::Sync:
        return "sync";
    }
    return "play";
}

const char* toString(MixerAction action) noexcept {
    switch (action) {
    case MixerAction::Volume:
        return "volume";
    case MixerAction::Gain:
        return "gain";
    case MixerAction::EqLow:
        return "eq-low";
    case MixerAction::EqMid:
        return "eq-mid";
    case MixerAction::EqHigh:
        return "eq-high";
    case MixerAction::Crossfader:
        return "crossfader";
    case MixerAction::Cue:
        return "cue";
    case MixerAction::Output:
        return "output";
    }
    return "volume";
}

const char* toString(BrowserAction action) noexcept {
    switch (action) {
    case BrowserAction::Import:
        return "import";
    case BrowserAction::LoadToDeck:
        return "load-to-deck";
    case BrowserAction::SelectRow:
        return "select-row";
    }
    return "import";
}

const char* toString(PluginChainAction action) noexcept {
    switch (action) {
    case PluginChainAction::Bypass:
        return "bypass";
    case PluginChainAction::Remove:
        return "remove";
    case PluginChainAction::MoveUp:
        return "move-up";
    case PluginChainAction::MoveDown:
        return "move-down";
    case PluginChainAction::OpenEditor:
        return "open-editor";
    case PluginChainAction::CloseEditor:
        return "close-editor";
    case PluginChainAction::Parameter:
        return "parameter";
    }
    return "bypass";
}

}
