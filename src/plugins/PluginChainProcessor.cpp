#include "plugins/PluginChainProcessor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#if DECKFLAXIA_HAS_JUCE
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#endif

#if DECKFLAXIA_HAS_JUCE
#ifndef JUCE_PLUGINHOST_VST3
#define JUCE_PLUGINHOST_VST3 0
#endif
#define DECKFLAXIA_HAS_JUCE_VST3_HOST (JUCE_PLUGINHOST_VST3 && (JUCE_MAC || JUCE_LINUX || JUCE_BSD))
#endif

namespace deckflaxia::plugins {

namespace {

constexpr const char* kDeterministicPluginId = "deterministic:gain";
constexpr const char* kGainParameterId = "gain";
constexpr const char* kRealFixtureManifestSchema = "deckflaxia-real-vst3-fixture-v1";
constexpr const char* kRealFixtureId = "real-vst3-fixture:deterministic_gain";
constexpr const char* kRealFixtureGeneratedBy = "DeckflaxiaRealVst3Fixture";

double clamp01(double value) noexcept {
    if (std::isnan(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

double rmsOfStereo(const float* interleavedStereo, std::uint32_t frameCount) noexcept {
    if (interleavedStereo == nullptr || frameCount == 0U) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * 2U;
        sum += static_cast<double>(interleavedStereo[index]) * static_cast<double>(interleavedStereo[index]);
        sum += static_cast<double>(interleavedStereo[index + 1U]) * static_cast<double>(interleavedStereo[index + 1U]);
    }
    return std::sqrt(sum / static_cast<double>(frameCount * 2U));
}

float peakOfStereo(const float* interleavedStereo, std::uint32_t frameCount) noexcept {
    float peak = 0.0F;
    if (interleavedStereo == nullptr) {
        return peak;
    }
    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * 2U;
        peak = std::max(peak, std::abs(interleavedStereo[index]));
        peak = std::max(peak, std::abs(interleavedStereo[index + 1U]));
    }
    return peak;
}

void ensureGainParameter(core::PluginDescriptor& plugin, double defaultValue) {
    auto found = std::find_if(plugin.parameters.begin(), plugin.parameters.end(), [](const auto& parameter) {
        return parameter.identifier == kGainParameterId;
    });
    if (found == plugin.parameters.end()) {
        plugin.parameters.push_back(core::PluginDescriptor::ParameterState{kGainParameterId, "Gain", clamp01(defaultValue)});
    } else {
        found->normalizedValue = clamp01(found->normalizedValue);
    }
}

double gainValue(const core::PluginDescriptor& plugin) noexcept {
    const auto found = std::find_if(plugin.parameters.begin(), plugin.parameters.end(), [](const auto& parameter) {
        return parameter.identifier == kGainParameterId;
    });
    return found == plugin.parameters.end() ? 0.5 : clamp01(found->normalizedValue);
}

std::string vst3PathFromIdentifier(const std::string& identifier) {
    constexpr const char* prefix = "vst3:";
    return identifier.rfind(prefix, 0) == 0 ? identifier.substr(5) : identifier;
}

bool jsonContainsField(const std::string& json, const std::string& key) {
    return json.find('"' + key + '"') != std::string::npos;
}

std::string unescapeJsonString(std::string value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 1U < value.size()) {
            const char escaped = value[++index];
            switch (escaped) {
            case 'n':
                unescaped.push_back('\n');
                break;
            case 'r':
                unescaped.push_back('\r');
                break;
            case 't':
                unescaped.push_back('\t');
                break;
            default:
                unescaped.push_back(escaped);
                break;
            }
        } else {
            unescaped.push_back(value[index]);
        }
    }
    return unescaped;
}

bool jsonStringField(const std::string& json, const std::string& key, std::string& value) {
    const auto keyPosition = json.find('"' + key + '"');
    if (keyPosition == std::string::npos) {
        return false;
    }
    const auto colon = json.find(':', keyPosition);
    if (colon == std::string::npos) {
        return false;
    }
    const auto firstValue = json.find_first_not_of(" \t\r\n", colon + 1U);
    if (firstValue == std::string::npos || json[firstValue] != '"') {
        return false;
    }
    const auto firstQuote = firstValue;
    std::string raw;
    bool escaped = false;
    for (auto index = firstQuote + 1U; index < json.size(); ++index) {
        const char character = json[index];
        if (!escaped && character == '"') {
            value = unescapeJsonString(raw);
            return true;
        }
        raw.push_back(character);
        escaped = !escaped && character == '\\';
        if (character != '\\') {
            escaped = false;
        }
    }
    return false;
}

bool jsonBoolField(const std::string& json, const std::string& key, bool& value) {
    const auto keyPosition = json.find('"' + key + '"');
    if (keyPosition == std::string::npos) {
        return false;
    }
    const auto colon = json.find(':', keyPosition);
    if (colon == std::string::npos) {
        return false;
    }
    const auto firstValue = json.find_first_not_of(" \t\r\n", colon + 1U);
    if (firstValue == std::string::npos) {
        return false;
    }
    if (json.compare(firstValue, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (json.compare(firstValue, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

std::filesystem::path resolveManifestPath(const std::filesystem::path& manifestPath, const std::string& rawPath) {
    std::filesystem::path path{rawPath};
    if (path.is_relative()) {
        path = manifestPath.parent_path() / path;
    }
    return path.lexically_normal();
}

RealVst3FixtureManifestResult manifestFailure(RealVst3FixtureManifestError error, std::string reason) {
    return RealVst3FixtureManifestResult{{}, error, std::move(reason)};
}

}

#if DECKFLAXIA_HAS_JUCE
class PluginEditorWindow final : public juce::DocumentWindow {
public:
    explicit PluginEditorWindow(const juce::String& name)
        : juce::DocumentWindow(name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                               juce::DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
    }

    void closeButtonPressed() override { setVisible(false); }
};
#endif

class OfflinePluginChainHost::Impl final {
public:
    PluginHostResult configure(PluginChainTargetKind target,
                               core::PluginChainDescriptor chain,
                               double sampleRateHz,
                               std::uint32_t maxBlockFrames) {
        target_ = target;
        sampleRateHz_ = sampleRateHz;
        maxBlockFrames_ = maxBlockFrames;
        chain_ = std::move(chain);
#if DECKFLAXIA_HAS_JUCE
        editorWindows_.clear();
        jucePlugins_.clear();
        jucePluginSlots_.clear();
#endif
        status_ = PluginProcessingStatus{};
        status_.juceAvailable = juceAvailable();
        status_.slotCount = chain_.plugins.size();
        status_.backend = status_.juceAvailable ? PluginProcessingBackendKind::JuceVst3 : PluginProcessingBackendKind::DeterministicFallback;
        status_.unavailableReason = status_.juceAvailable ? "no VST3 plugin instantiated" : "JUCE unavailable; deterministic AGPL-compatible fixture processor only";
        status_.latencyFrames = 0;

        for (auto& plugin : chain_.plugins) {
            if (isDeterministicTestPluginId(plugin.identifier)) {
                ensureGainParameter(plugin, 0.5);
                ++status_.activeSlotCount;
            } else {
#if DECKFLAXIA_HAS_JUCE
                continue;
#else
                ++status_.unavailableSlotCount;
#endif
            }
            status_.latencyFrames += plugin.latencyFrames;
        }

#if DECKFLAXIA_HAS_JUCE
        instantiateJucePlugins();
#endif
        if (chain_.plugins.empty()) {
            status_.unavailableReason.clear();
        }
        return PluginHostResult::success();
    }

    PluginHostResult setSlotBypass(std::size_t slotIndex, bool bypassed) noexcept {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginHostResult::failure(PluginHostError::InvalidSlot);
        }
        chain_.plugins[slotIndex].bypassed = bypassed;
        return PluginHostResult::success();
    }

    PluginHostResult setParameter(std::size_t slotIndex, const std::string& parameterId, double normalizedValue) noexcept {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginHostResult::failure(PluginHostError::InvalidSlot);
        }
        auto& plugin = chain_.plugins[slotIndex];
        auto found = std::find_if(plugin.parameters.begin(), plugin.parameters.end(), [&](const auto& parameter) {
            return parameter.identifier == parameterId;
        });
        if (found == plugin.parameters.end()) {
            return PluginHostResult::failure(PluginHostError::InvalidParameter);
        }
        found->normalizedValue = clamp01(normalizedValue);
#if DECKFLAXIA_HAS_JUCE
        applyJuceParameter(slotIndex, parameterId, found->normalizedValue);
#endif
        return PluginHostResult::success();
    }

    double parameter(std::size_t slotIndex, const std::string& parameterId) const noexcept {
        if (slotIndex >= chain_.plugins.size()) {
            return 0.0;
        }
        const auto& plugin = chain_.plugins[slotIndex];
        const auto found = std::find_if(plugin.parameters.begin(), plugin.parameters.end(), [&](const auto& parameter) {
            return parameter.identifier == parameterId;
        });
        return found == plugin.parameters.end() ? 0.0 : found->normalizedValue;
    }

    PluginAudioMetrics processReplacing(float* interleavedStereo, std::uint32_t frameCount, bool forceBypass) noexcept {
        PluginAudioMetrics metrics;
        metrics.inputRms = rmsOfStereo(interleavedStereo, frameCount);
        if (interleavedStereo == nullptr || frameCount == 0U || forceBypass) {
            metrics.outputRms = metrics.inputRms;
            metrics.peakMagnitude = peakOfStereo(interleavedStereo, frameCount);
            return metrics;
        }

#if DECKFLAXIA_HAS_JUCE
        processJuce(interleavedStereo, frameCount);
#endif
        for (const auto& plugin : chain_.plugins) {
            if (plugin.bypassed || !isDeterministicTestPluginId(plugin.identifier)) {
                continue;
            }
            const auto gain = static_cast<float>(gainValue(plugin));
            for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
                const auto index = static_cast<std::size_t>(frame) * 2U;
                interleavedStereo[index] *= gain;
                interleavedStereo[index + 1U] *= gain;
            }
        }
        metrics.outputRms = rmsOfStereo(interleavedStereo, frameCount);
        metrics.peakMagnitude = peakOfStereo(interleavedStereo, frameCount);
        metrics.changedAudio = std::abs(metrics.outputRms - metrics.inputRms) > 0.000001;
        return metrics;
    }

    const core::PluginChainDescriptor& chainState() const noexcept { return chain_; }

    PluginHostResult snapshotState(std::size_t slotIndex, PluginStateSnapshot& snapshot) const {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginHostResult::failure(PluginHostError::InvalidSlot);
        }
        snapshot.bytes.clear();
#if DECKFLAXIA_HAS_JUCE
        const auto juceIndex = juceIndexForSlot(slotIndex);
        if (juceIndex < jucePlugins_.size()) {
            juce::MemoryBlock block;
            jucePlugins_[juceIndex]->getStateInformation(block);
            if (block.getSize() > 0U) {
                const auto* data = static_cast<const std::uint8_t*>(block.getData());
                snapshot.bytes.assign(data, data + block.getSize());
            }
            return PluginHostResult::success();
        }
#endif
        if (isDeterministicTestPluginId(chain_.plugins[slotIndex].identifier)) {
            const auto gain = static_cast<std::uint8_t>(std::lround(gainValue(chain_.plugins[slotIndex]) * 255.0));
            snapshot.bytes.push_back(gain);
            return PluginHostResult::success();
        }
        return PluginHostResult::failure(PluginHostError::PluginUnavailable);
    }

    PluginHostResult restoreState(std::size_t slotIndex, const PluginStateSnapshot& snapshot) {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginHostResult::failure(PluginHostError::InvalidSlot);
        }
#if DECKFLAXIA_HAS_JUCE
        const auto juceIndex = juceIndexForSlot(slotIndex);
        if (juceIndex < jucePlugins_.size()) {
            jucePlugins_[juceIndex]->setStateInformation(snapshot.bytes.data(), static_cast<int>(snapshot.bytes.size()));
            syncJuceParametersToDescriptor(slotIndex);
            return PluginHostResult::success();
        }
#endif
        if (isDeterministicTestPluginId(chain_.plugins[slotIndex].identifier) && !snapshot.bytes.empty()) {
            return setParameter(slotIndex, kGainParameterId, static_cast<double>(snapshot.bytes[0]) / 255.0);
        }
        return PluginHostResult::failure(PluginHostError::PluginUnavailable);
    }

    PluginProcessingStatus status() const noexcept { return status_; }

    PluginEditorWindowStatus openSeparateEditorWindow(std::size_t slotIndex) {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginEditorWindowStatus{false, false, false, "invalid-slot"};
        }
        if (isDeterministicTestPluginId(chain_.plugins[slotIndex].identifier)) {
            return PluginEditorWindowStatus{false, false, true, "generic-parameter-surface deterministic fixture has no native editor"};
        }
#if DECKFLAXIA_HAS_JUCE
        const auto found = std::find(jucePluginSlots_.begin(), jucePluginSlots_.end(), slotIndex);
        if (found == jucePluginSlots_.end()) {
            return PluginEditorWindowStatus{false, false, true, "native-editor-unavailable plugin instance not loaded"};
        }
        const auto juceIndex = static_cast<std::size_t>(std::distance(jucePluginSlots_.begin(), found));
        auto* plugin = jucePlugins_[juceIndex].get();
        if (plugin == nullptr || !plugin->hasEditor()) {
            return PluginEditorWindowStatus{false, false, true, "generic-parameter-surface plugin reports no native editor"};
        }
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
            juce::MessageManager::callAsync([this, slotIndex] { (void)openSeparateEditorWindow(slotIndex); });
            return PluginEditorWindowStatus{true, false, false, "native-editor-open-pending message-thread-handoff"};
        }
        if (editorWindows_.size() < chain_.plugins.size()) {
            editorWindows_.resize(chain_.plugins.size());
        }
        if (editorWindows_[slotIndex] == nullptr) {
            std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditor());
            if (editor == nullptr) {
                return PluginEditorWindowStatus{false, false, true, "generic-parameter-surface createEditor returned null"};
            }
            auto window = std::make_unique<PluginEditorWindow>(juce::String(chain_.plugins[slotIndex].displayName) + " Editor");
            window->setContentOwned(editor.release(), true);
            window->centreWithSize(window->getWidth(), window->getHeight());
            editorWindows_[slotIndex] = std::move(window);
        }
        editorWindows_[slotIndex]->setVisible(true);
        editorWindows_[slotIndex]->toFront(true);
        return PluginEditorWindowStatus{true, editorWindows_[slotIndex]->isVisible(), false, "native-editor-open separate-window message-thread"};
#else
        return PluginEditorWindowStatus{false, false, true, "native-editor-unavailable DECKFLAXIA_HAS_JUCE=0"};
#endif
    }

    PluginEditorWindowStatus closeSeparateEditorWindow(std::size_t slotIndex) {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginEditorWindowStatus{false, false, false, "invalid-slot"};
        }
#if DECKFLAXIA_HAS_JUCE
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
            juce::MessageManager::callAsync([this, slotIndex] { (void)closeSeparateEditorWindow(slotIndex); });
            return PluginEditorWindowStatus{true, true, false, "native-editor-close-pending message-thread-handoff"};
        }
        const auto nativeAvailable = std::find(jucePluginSlots_.begin(), jucePluginSlots_.end(), slotIndex) != jucePluginSlots_.end();
        if (slotIndex < editorWindows_.size() && editorWindows_[slotIndex] != nullptr) {
            editorWindows_[slotIndex]->setVisible(false);
        }
        return PluginEditorWindowStatus{nativeAvailable, false, !nativeAvailable, nativeAvailable ? "native-editor-closed" : "generic-parameter-surface closed-no-native-window"};
#else
        return PluginEditorWindowStatus{false, false, true, "native-editor-unavailable DECKFLAXIA_HAS_JUCE=0"};
#endif
    }

private:
    static bool juceAvailable() noexcept {
#if DECKFLAXIA_HAS_JUCE
        return true;
#else
        return false;
#endif
    }

#if DECKFLAXIA_HAS_JUCE
    void instantiateJucePlugins() {
#if DECKFLAXIA_HAS_JUCE_VST3_HOST
        if (!juceFormatsRegistered_) {
            formatManager_.addFormat(new juce::VST3PluginFormat());
            juceFormatsRegistered_ = true;
        }
        juceBuffer_.setSize(2, static_cast<int>(maxBlockFrames_), false, false, true);
        for (auto& plugin : chain_.plugins) {
            if (isDeterministicTestPluginId(plugin.identifier)) {
                continue;
            }
            juce::OwnedArray<juce::PluginDescription> descriptions;
            juce::VST3PluginFormat vst3;
            vst3.findAllTypesForFile(descriptions, juce::String(vst3PathFromIdentifier(plugin.identifier)));
            if (descriptions.isEmpty()) {
                ++status_.unavailableSlotCount;
                continue;
            }
            juce::String error;
            auto instance = formatManager_.createPluginInstance(*descriptions[0], sampleRateHz_, static_cast<int>(maxBlockFrames_), error);
            if (instance == nullptr) {
                ++status_.unavailableSlotCount;
                status_.unavailableReason = error.toStdString();
                continue;
            }
            instance->prepareToPlay(sampleRateHz_, static_cast<int>(maxBlockFrames_));
            ensureGainParameter(plugin, 0.5);
            plugin.latencyFrames = static_cast<std::uint32_t>(std::max(0, instance->getLatencySamples()));
            status_.latencyFrames += plugin.latencyFrames;
            const auto slotIndex = static_cast<std::size_t>(&plugin - chain_.plugins.data());
            jucePluginSlots_.push_back(slotIndex);
            jucePlugins_.push_back(std::move(instance));
            applyJuceParameter(slotIndex, kGainParameterId, gainValue(plugin));
            status_.realVst3Instantiated = true;
            ++status_.activeSlotCount;
        }
        if (status_.realVst3Instantiated) {
            status_.unavailableReason.clear();
        }
#else
        status_.unavailableSlotCount = chain_.plugins.size();
        status_.unavailableReason = "JUCE VST3 host support unavailable; deterministic AGPL-compatible fixture processor only";
#endif
    }

    std::size_t juceIndexForSlot(std::size_t slotIndex) const noexcept {
        const auto found = std::find(jucePluginSlots_.begin(), jucePluginSlots_.end(), slotIndex);
        return found == jucePluginSlots_.end() ? jucePlugins_.size() : static_cast<std::size_t>(std::distance(jucePluginSlots_.begin(), found));
    }

    juce::AudioProcessorParameter* juceParameterForSlot(std::size_t slotIndex, const std::string& parameterId) const noexcept {
        const auto juceIndex = juceIndexForSlot(slotIndex);
        if (juceIndex >= jucePlugins_.size()) {
            return nullptr;
        }
        for (auto* parameter : jucePlugins_[juceIndex]->getParameters()) {
            if (auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)) {
                if (parameterWithId->paramID == parameterId.c_str()) {
                    return parameter;
                }
            }
        }
        return nullptr;
    }

    void applyJuceParameter(std::size_t slotIndex, const std::string& parameterId, double normalizedValue) noexcept {
        if (auto* parameter = juceParameterForSlot(slotIndex, parameterId)) {
            parameter->setValueNotifyingHost(static_cast<float>(clamp01(normalizedValue)));
        }
    }

    void syncJuceParametersToDescriptor(std::size_t slotIndex) noexcept {
        if (slotIndex >= chain_.plugins.size()) {
            return;
        }
        auto& plugin = chain_.plugins[slotIndex];
        for (auto& parameterState : plugin.parameters) {
            if (auto* parameter = juceParameterForSlot(slotIndex, parameterState.identifier)) {
                parameterState.normalizedValue = clamp01(parameter->getValue());
            }
        }
    }

    void processJuce(float* interleavedStereo, std::uint32_t frameCount) noexcept {
        if (jucePlugins_.empty() || interleavedStereo == nullptr || frameCount > maxBlockFrames_) {
            return;
        }
        for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
            const auto index = static_cast<std::size_t>(frame) * 2U;
            juceBuffer_.setSample(0, static_cast<int>(frame), interleavedStereo[index]);
            juceBuffer_.setSample(1, static_cast<int>(frame), interleavedStereo[index + 1U]);
        }
        juce::MidiBuffer midi;
        for (std::size_t index = 0; index < jucePlugins_.size(); ++index) {
            const auto slot = index < jucePluginSlots_.size() ? jucePluginSlots_[index] : chain_.plugins.size();
            if (slot >= chain_.plugins.size() || chain_.plugins[slot].bypassed) {
                continue;
            }
            jucePlugins_[index]->processBlock(juceBuffer_, midi);
        }
        for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
            const auto index = static_cast<std::size_t>(frame) * 2U;
            interleavedStereo[index] = juceBuffer_.getSample(0, static_cast<int>(frame));
            interleavedStereo[index + 1U] = juceBuffer_.getSample(1, static_cast<int>(frame));
        }
    }

    juce::AudioPluginFormatManager formatManager_;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> jucePlugins_;
    std::vector<std::size_t> jucePluginSlots_;
    std::vector<std::unique_ptr<PluginEditorWindow>> editorWindows_;
    juce::AudioBuffer<float> juceBuffer_;
    bool juceFormatsRegistered_{};
#endif

    PluginChainTargetKind target_{PluginChainTargetKind::Deck};
    core::PluginChainDescriptor chain_;
    double sampleRateHz_{48000.0};
    std::uint32_t maxBlockFrames_{512};
    PluginProcessingStatus status_{};
};

OfflinePluginChainHost::OfflinePluginChainHost() : impl_(std::make_unique<Impl>()) {}
OfflinePluginChainHost::~OfflinePluginChainHost() = default;
OfflinePluginChainHost::OfflinePluginChainHost(OfflinePluginChainHost&&) noexcept = default;
OfflinePluginChainHost& OfflinePluginChainHost::operator=(OfflinePluginChainHost&&) noexcept = default;

PluginHostResult OfflinePluginChainHost::configure(PluginChainTargetKind target,
                                                   core::PluginChainDescriptor chain,
                                                   double sampleRateHz,
                                                   std::uint32_t maxBlockFrames) {
    return impl_->configure(target, std::move(chain), sampleRateHz, maxBlockFrames);
}

PluginHostResult OfflinePluginChainHost::setSlotBypass(std::size_t slotIndex, bool bypassed) noexcept {
    return impl_->setSlotBypass(slotIndex, bypassed);
}

PluginHostResult OfflinePluginChainHost::setParameter(std::size_t slotIndex, const std::string& parameterId, double normalizedValue) noexcept {
    return impl_->setParameter(slotIndex, parameterId, normalizedValue);
}

double OfflinePluginChainHost::parameter(std::size_t slotIndex, const std::string& parameterId) const noexcept {
    return impl_->parameter(slotIndex, parameterId);
}

const core::PluginChainDescriptor& OfflinePluginChainHost::chainState() const noexcept {
    return impl_->chainState();
}

PluginHostResult OfflinePluginChainHost::snapshotState(std::size_t slotIndex, PluginStateSnapshot& snapshot) const {
    return impl_->snapshotState(slotIndex, snapshot);
}

PluginHostResult OfflinePluginChainHost::restoreState(std::size_t slotIndex, const PluginStateSnapshot& snapshot) {
    return impl_->restoreState(slotIndex, snapshot);
}

PluginProcessingStatus OfflinePluginChainHost::status() const noexcept {
    return impl_->status();
}

PluginEditorWindowStatus OfflinePluginChainHost::openSeparateEditorWindow(std::size_t slotIndex) const {
    return impl_->openSeparateEditorWindow(slotIndex);
}

PluginEditorWindowStatus OfflinePluginChainHost::closeSeparateEditorWindow(std::size_t slotIndex) const {
    return impl_->closeSeparateEditorWindow(slotIndex);
}

PluginAudioMetrics OfflinePluginChainHost::processReplacing(float* interleavedStereo,
                                                            std::uint32_t frameCount,
                                                            bool forceBypass) noexcept {
    return impl_->processReplacing(interleavedStereo, frameCount, forceBypass);
}

bool isDeterministicTestPluginId(const std::string& pluginId) noexcept {
    return pluginId == kDeterministicPluginId || pluginId.find("deterministic-gain") != std::string::npos;
}

core::PluginDescriptor makeDeterministicGainPlugin(double normalizedGain, bool bypassed) {
    core::PluginDescriptor plugin{kDeterministicPluginId, "Deterministic Gain Fixture", bypassed};
    plugin.parameters.push_back(core::PluginDescriptor::ParameterState{kGainParameterId, "Gain", clamp01(normalizedGain)});
    plugin.latencyFrames = 0;
    return plugin;
}

core::PluginDescriptor makeRealVst3FixturePlugin(const RealVst3FixtureManifest& manifest, bool bypassed) {
    core::PluginDescriptor plugin{"vst3:" + manifest.bundlePath.string(), manifest.fixtureId, bypassed};
    plugin.parameters.push_back(core::PluginDescriptor::ParameterState{kGainParameterId, "Gain", 0.5});
    plugin.latencyFrames = 0;
    return plugin;
}

RealVst3FixtureManifestResult loadRealVst3FixtureManifest(const std::filesystem::path& manifestPath) {
    if (!std::filesystem::exists(manifestPath)) {
        return manifestFailure(RealVst3FixtureManifestError::ManifestMissing, "real VST3 fixture manifest missing: " + manifestPath.string());
    }
    std::ifstream input(manifestPath);
    if (!input.good()) {
        return manifestFailure(RealVst3FixtureManifestError::ManifestUnreadable, "real VST3 fixture manifest unreadable: " + manifestPath.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto json = buffer.str();

    std::string schema;
    if (!jsonStringField(json, "schema", schema) || schema != kRealFixtureManifestSchema) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidSchema, "real VST3 fixture manifest schema must be deckflaxia-real-vst3-fixture-v1");
    }
    std::string fixtureId;
    if (!jsonStringField(json, "fixture_id", fixtureId) || fixtureId != kRealFixtureId || fixtureId == kDeterministicPluginId) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidFixtureId, "real VST3 fixture manifest fixture_id must identify the source-built real fixture");
    }
    std::string format;
    if (!jsonStringField(json, "format", format) || format != "vst3") {
        return manifestFailure(RealVst3FixtureManifestError::InvalidFormat, "real VST3 fixture manifest format must be vst3");
    }
    bool expected = false;
    if (!jsonBoolField(json, "expected", expected) || !expected) {
        return manifestFailure(RealVst3FixtureManifestError::ExpectedNotTrue, "real VST3 fixture manifest expected must be true");
    }
    std::string bundlePathRaw;
    if (!jsonStringField(json, "bundle_path", bundlePathRaw) || bundlePathRaw.empty()) {
        return manifestFailure(RealVst3FixtureManifestError::MissingBundlePath, "real VST3 fixture manifest bundle_path is required");
    }
    const auto bundlePath = resolveManifestPath(manifestPath, bundlePathRaw);
    if (bundlePath.extension() != ".vst3" || !std::filesystem::exists(bundlePath)) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidBundlePath, "real VST3 fixture bundle_path must name an existing .vst3 bundle: " + bundlePath.string());
    }
    std::string source;
    if (!jsonStringField(json, "source", source) || source != "build-tree") {
        return manifestFailure(RealVst3FixtureManifestError::InvalidSource, "real VST3 fixture manifest source must be build-tree");
    }
    std::string licenseNotice;
    if (!jsonStringField(json, "license_notice", licenseNotice) || licenseNotice.empty()) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidLicenseNotice, "real VST3 fixture manifest license_notice is required");
    }
    std::string generatedBy;
    if (!jsonStringField(json, "generated_by", generatedBy) || generatedBy != kRealFixtureGeneratedBy) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidGeneratedBy, "real VST3 fixture manifest generated_by must be DeckflaxiaRealVst3Fixture");
    }
    if (!jsonContainsField(json, "binary_path_if_applicable")) {
        return manifestFailure(RealVst3FixtureManifestError::InvalidBundlePath, "real VST3 fixture manifest binary_path_if_applicable field is required");
    }
    std::string binaryPathRaw;
    RealVst3FixtureManifest manifest{fixtureId, bundlePath, {}};
    if (jsonStringField(json, "binary_path_if_applicable", binaryPathRaw) && !binaryPathRaw.empty()) {
        manifest.binaryPathIfApplicable = resolveManifestPath(manifestPath, binaryPathRaw);
    }
    return RealVst3FixtureManifestResult{manifest, RealVst3FixtureManifestError::None, {}};
}

const char* toString(PluginProcessingBackendKind backend) noexcept {
    switch (backend) {
    case PluginProcessingBackendKind::Unavailable:
        return "unavailable";
    case PluginProcessingBackendKind::DeterministicFallback:
        return "deterministic-fallback";
    case PluginProcessingBackendKind::JuceVst3:
        return "juce-vst3";
    }
    return "unavailable";
}

const char* toString(PluginHostError error) noexcept {
    switch (error) {
    case PluginHostError::None:
        return "none";
    case PluginHostError::InvalidSlot:
        return "invalid-slot";
    case PluginHostError::InvalidParameter:
        return "invalid-parameter";
    case PluginHostError::PluginUnavailable:
        return "plugin-unavailable";
    case PluginHostError::HostUnavailable:
        return "host-unavailable";
    }
    return "host-unavailable";
}

const char* toString(RealVst3FixtureManifestError error) noexcept {
    switch (error) {
    case RealVst3FixtureManifestError::None:
        return "none";
    case RealVst3FixtureManifestError::ManifestMissing:
        return "manifest-missing";
    case RealVst3FixtureManifestError::ManifestUnreadable:
        return "manifest-unreadable";
    case RealVst3FixtureManifestError::InvalidSchema:
        return "invalid-schema";
    case RealVst3FixtureManifestError::InvalidFixtureId:
        return "invalid-fixture-id";
    case RealVst3FixtureManifestError::InvalidFormat:
        return "invalid-format";
    case RealVst3FixtureManifestError::ExpectedNotTrue:
        return "expected-not-true";
    case RealVst3FixtureManifestError::MissingBundlePath:
        return "missing-bundle-path";
    case RealVst3FixtureManifestError::InvalidBundlePath:
        return "invalid-bundle-path";
    case RealVst3FixtureManifestError::InvalidSource:
        return "invalid-source";
    case RealVst3FixtureManifestError::InvalidLicenseNotice:
        return "invalid-license-notice";
    case RealVst3FixtureManifestError::InvalidGeneratedBy:
        return "invalid-generated-by";
    }
    return "manifest-unreadable";
}

}
