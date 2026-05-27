#include "plugins/PluginChainProcessor.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#if DJAPP_HAS_JUCE
#include <JuceHeader.h>
#endif

namespace djapp::plugins {

namespace {

constexpr const char* kDeterministicPluginId = "deterministic:gain";
constexpr const char* kGainParameterId = "gain";

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

}

#if DJAPP_HAS_JUCE
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
#if DJAPP_HAS_JUCE
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
#if DJAPP_HAS_JUCE
                continue;
#else
                ++status_.unavailableSlotCount;
#endif
            }
            status_.latencyFrames += plugin.latencyFrames;
        }

#if DJAPP_HAS_JUCE
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

#if DJAPP_HAS_JUCE
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
    PluginProcessingStatus status() const noexcept { return status_; }

    PluginEditorWindowStatus openSeparateEditorWindow(std::size_t slotIndex) {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginEditorWindowStatus{false, false, false, "invalid-slot"};
        }
        if (isDeterministicTestPluginId(chain_.plugins[slotIndex].identifier)) {
            return PluginEditorWindowStatus{false, false, true, "generic-parameter-surface deterministic fixture has no native editor"};
        }
#if DJAPP_HAS_JUCE
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
        return PluginEditorWindowStatus{false, false, true, "native-editor-unavailable DJAPP_HAS_JUCE=0"};
#endif
    }

    PluginEditorWindowStatus closeSeparateEditorWindow(std::size_t slotIndex) {
        if (slotIndex >= chain_.plugins.size()) {
            return PluginEditorWindowStatus{false, false, false, "invalid-slot"};
        }
#if DJAPP_HAS_JUCE
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
        return PluginEditorWindowStatus{false, false, true, "native-editor-unavailable DJAPP_HAS_JUCE=0"};
#endif
    }

private:
    static bool juceAvailable() noexcept {
#if DJAPP_HAS_JUCE
        return true;
#else
        return false;
#endif
    }

#if DJAPP_HAS_JUCE
    void instantiateJucePlugins() {
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
            plugin.latencyFrames = static_cast<std::uint32_t>(std::max(0, instance->getLatencySamples()));
            status_.latencyFrames += plugin.latencyFrames;
            jucePluginSlots_.push_back(static_cast<std::size_t>(&plugin - chain_.plugins.data()));
            jucePlugins_.push_back(std::move(instance));
            status_.realVst3Instantiated = true;
            ++status_.activeSlotCount;
        }
        if (status_.realVst3Instantiated) {
            status_.unavailableReason.clear();
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
        for (auto& plugin : jucePlugins_) {
            plugin->processBlock(juceBuffer_, midi);
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

}
