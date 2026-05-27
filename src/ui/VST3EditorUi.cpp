#include "ui/VST3EditorUi.h"

#include "decks/FourDeckPlaybackCore.h"
#include "persistence/Persistence.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace djapp::ui {

namespace {

std::string targetLabel(plugins::PluginChainTargetKind target) {
    return target == plugins::PluginChainTargetKind::Master ? "master" : "deck";
}

double clamp01(double value) noexcept {
    if (std::isnan(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

std::string evidencePath(const char* fileName) {
    return (std::filesystem::path{".omo/evidence/real-playable-juce"} / fileName).string();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

std::string stateMatches(const core::PluginChainDescriptor& a, const core::PluginChainDescriptor& b) {
    if (a.identifier != b.identifier || a.plugins.size() != b.plugins.size()) {
        return "0";
    }
    for (std::size_t index = 0; index < a.plugins.size(); ++index) {
        const auto& left = a.plugins[index];
        const auto& right = b.plugins[index];
        if (left.identifier != right.identifier || left.bypassed != right.bypassed || left.parameters.size() != right.parameters.size()) {
            return "0";
        }
        for (std::size_t param = 0; param < left.parameters.size(); ++param) {
            if (left.parameters[param].identifier != right.parameters[param].identifier ||
                std::abs(left.parameters[param].normalizedValue - right.parameters[param].normalizedValue) > 0.000001) {
                return "0";
            }
        }
    }
    return "1";
}

float renderWithEditorLifecycle(bool bypassed) {
    decks::FourDeckPlaybackCore core;
    const auto deckId = core::DeckId::fromIndex(0).value;
    auto media = decks::PreparedAudioMedia::deterministicTestWav(4096, 48000);
    if (!core.loadDeck(deckId, decks::AudioDeckMediaReference::deterministicTestWav(std::move(media))).ok()) {
        return 0.0F;
    }
    core::PluginChainDescriptor chain{"deck-a", {plugins::makeDeterministicGainPlugin(0.25, bypassed)}};
    if (!core.setDeckPluginChain(deckId, chain).ok()) {
        return 0.0F;
    }
    const auto open = core.deckPluginChain(deckId).openSeparateEditorWindow(0);
    const auto close = core.deckPluginChain(deckId).closeSeparateEditorWindow(0);
    (void)open;
    (void)close;
    if (core.play(deckId) != decks::FourDeckPlaybackError::None) {
        return 0.0F;
    }
    const auto render = core.renderOffline(audio::AudioRenderConfiguration{48000, 512}, 1);
    if (!render.ok()) {
        return 0.0F;
    }
    const auto& buffer = core.lastInterleavedOutput();
    double sum = 0.0;
    for (std::uint32_t frame = 0; frame < 512U; ++frame) {
        const auto index = static_cast<std::size_t>(frame) * decks::kFourDeckOutputChannels;
        sum += static_cast<double>(buffer[index]) * static_cast<double>(buffer[index]);
        sum += static_cast<double>(buffer[index + 1U]) * static_cast<double>(buffer[index + 1U]);
    }
    return static_cast<float>(std::sqrt(sum / 1024.0));
}

} // namespace

PluginChainEditorViewModel buildPluginChainEditorModel(plugins::PluginChainTargetKind target,
                                                       const core::PluginChainDescriptor& chain,
                                                       const plugins::PluginProcessingStatus& status) {
    PluginChainEditorViewModel model;
    model.targetLabel = targetLabel(target);
    model.stateStatus = "state-ready: save reload parameter snapshot available";
    model.sandboxStatus = "sandbox-boundary: sandbox-helper-status generic-parameters-only cross-process-native-editor-embedding-deferred";
    model.nativeEditorStatus = status.juceAvailable ? "native-editor-boundary: message-thread separate-window eligible" : "native-editor-boundary: unavailable generic-parameters-only";
    model.slots.reserve(chain.plugins.size());

    for (std::size_t index = 0; index < chain.plugins.size(); ++index) {
        const auto& plugin = chain.plugins[index];
        PluginSlotEditorViewModel slot;
        slot.componentName = model.targetLabel + "-plugin-slot-" + std::to_string(index + 1U);
        slot.displayName = plugin.displayName.empty() ? plugin.identifier : plugin.displayName;
        slot.targetLabel = model.targetLabel;
        slot.slotIndex = index;
        slot.bypassed = plugin.bypassed;
        slot.removable = true;
        slot.canMoveUp = index > 0U;
        slot.canMoveDown = index + 1U < chain.plugins.size();
        slot.surface = status.realVst3Instantiated ? PluginEditorSurfaceKind::NativeSeparateWindow : PluginEditorSurfaceKind::GenericParameterSurface;
        if (!status.juceAvailable) {
            slot.surface = PluginEditorSurfaceKind::NativeEditorUnavailable;
        }
        slot.processingStatus = std::string{"backend="} + plugins::toString(status.backend) + " active-slots=" + std::to_string(status.activeSlotCount);
        slot.boundaryStatus = status.realVst3Instantiated ? "in-process native editor window" : "generic parameter surface/status; sandboxed native editor embedding deferred";
        slot.controls = {"bypass", "remove", "move-up", "move-down", "open-editor", "close-editor"};
        slot.parameters.reserve(plugin.parameters.size());
        for (const auto& parameter : plugin.parameters) {
            slot.parameters.push_back(PluginParameterViewModel{slot.componentName + "-parameter-" + parameter.identifier,
                                                               parameter.displayName,
                                                               parameter.identifier,
                                                               clamp01(parameter.normalizedValue)});
        }
        model.slots.push_back(std::move(slot));
    }

    return model;
}

bool setPluginSlotBypass(core::PluginChainDescriptor& chain, std::size_t slotIndex, bool bypassed) noexcept {
    if (slotIndex >= chain.plugins.size()) {
        return false;
    }
    chain.plugins[slotIndex].bypassed = bypassed;
    return true;
}

bool removePluginSlot(core::PluginChainDescriptor& chain, std::size_t slotIndex) noexcept {
    if (slotIndex >= chain.plugins.size()) {
        return false;
    }
    chain.plugins.erase(chain.plugins.begin() + static_cast<std::ptrdiff_t>(slotIndex));
    return true;
}

bool movePluginSlot(core::PluginChainDescriptor& chain, std::size_t fromSlot, std::size_t toSlot) noexcept {
    if (fromSlot >= chain.plugins.size() || toSlot >= chain.plugins.size() || fromSlot == toSlot) {
        return false;
    }
    auto plugin = std::move(chain.plugins[fromSlot]);
    chain.plugins.erase(chain.plugins.begin() + static_cast<std::ptrdiff_t>(fromSlot));
    chain.plugins.insert(chain.plugins.begin() + static_cast<std::ptrdiff_t>(toSlot), std::move(plugin));
    return true;
}

bool setPluginParameter(core::PluginChainDescriptor& chain,
                        std::size_t slotIndex,
                        const std::string& parameterId,
                        double normalizedValue) noexcept {
    if (slotIndex >= chain.plugins.size()) {
        return false;
    }
    auto& params = chain.plugins[slotIndex].parameters;
    auto found = std::find_if(params.begin(), params.end(), [&](const auto& parameter) {
        return parameter.identifier == parameterId;
    });
    if (found == params.end()) {
        return false;
    }
    found->normalizedValue = clamp01(normalizedValue);
    return true;
}

bool saveAndReloadPluginChain(core::PluginChainDescriptor chain, core::PluginChainDescriptor& reloaded) {
    persistence::PersistenceService service;
    const auto saved = service.pluginChains().save(chain);
    const auto loaded = service.pluginChains().load(chain.identifier);
    if (!saved.ok() || !loaded.ok()) {
        return false;
    }
    reloaded = loaded.value;
    return true;
}

const char* toString(PluginEditorSurfaceKind surface) noexcept {
    switch (surface) {
    case PluginEditorSurfaceKind::NativeSeparateWindow:
        return "native-separate-window";
    case PluginEditorSurfaceKind::GenericParameterSurface:
        return "generic-parameter-surface";
    case PluginEditorSurfaceKind::NativeEditorUnavailable:
        return "native-editor-unavailable";
    }
    return "native-editor-unavailable";
}

int runVst3EditorSmokeTest(std::ostream& output, const VST3EditorSmokeOptions& options) {
    output << "vst3-editor-smoke-test: task-11\n";
    output << "fixtures=" << options.fixtureDirectory.string() << '\n';

    plugins::OfflinePluginChainHost host;
    core::PluginChainDescriptor deckChain{"deck-a", {plugins::makeDeterministicGainPlugin(0.5, false)}};
    auto noEditorPlugin = plugins::makeDeterministicGainPlugin(0.75, false);
    noEditorPlugin.identifier = "deterministic:no-editor";
    noEditorPlugin.displayName = "Deterministic No Editor Fixture";
    core::PluginChainDescriptor noEditorChain{"master", {noEditorPlugin}};

    const auto configured = host.configure(plugins::PluginChainTargetKind::Deck, deckChain, 48000.0, 512);
    const auto openStatus = host.openSeparateEditorWindow(0);
    const auto closeStatus = host.closeSeparateEditorWindow(0);
    const auto model = buildPluginChainEditorModel(plugins::PluginChainTargetKind::Deck, host.chainState(), host.status());

    core::PluginChainDescriptor reloaded;
    const auto savedReloaded = saveAndReloadPluginChain(deckChain, reloaded);
    const auto processed = renderWithEditorLifecycle(false);
    const auto bypassed = renderWithEditorLifecycle(true);
    const auto processingPreserved = processed > 0.0F && bypassed > 0.0F && std::abs(processed - bypassed) > 0.0001F;

    plugins::OfflinePluginChainHost noEditorHost;
    const auto noEditorConfigured = noEditorHost.configure(plugins::PluginChainTargetKind::Master, noEditorChain, 48000.0, 512);
    const auto noEditorOpen = noEditorHost.openSeparateEditorWindow(0);
    const auto noEditorModel = buildPluginChainEditorModel(plugins::PluginChainTargetKind::Master, noEditorHost.chainState(), noEditorHost.status());

    std::ostringstream editorLog;
    editorLog << std::fixed << std::setprecision(6)
              << "vst3-editor-smoke-test: separate-window\n"
              << "configured=" << (configured.ok() ? 1 : 0)
              << " backend=" << plugins::toString(host.status().backend)
              << " juce=" << (host.status().juceAvailable ? 1 : 0)
              << " real-vst3-instantiated=" << (host.status().realVst3Instantiated ? 1 : 0) << '\n'
              << "open-requested=1 native-editor-open=" << (openStatus.open ? 1 : 0)
              << " generic-parameters=" << (openStatus.genericParameterSurfaceAvailable ? 1 : 0)
              << " status=" << openStatus.statusText << '\n'
              << "close-requested=1 native-editor-open-after-close=" << (closeStatus.open ? 1 : 0)
              << " status=" << closeStatus.statusText << '\n'
              << "model-component=" << model.componentName
              << " slots=" << model.slots.size()
              << " surface=" << (model.slots.empty() ? "none" : toString(model.slots[0].surface)) << '\n'
              << "state-save-reload=" << (savedReloaded ? 1 : 0)
              << " state-identical=" << (savedReloaded ? stateMatches(deckChain, reloaded) : "0") << '\n'
              << "processing-preserved=" << (processingPreserved ? 1 : 0)
              << " processed-rms=" << processed << " bypass-rms=" << bypassed << '\n'
              << "sandbox-boundary=in-process-editor-only cross-process-native-editor-embedding-deferred\n";
    if (!options.screenshotPath.empty()) {
        if (openStatus.open && openStatus.nativeEditorAvailable) {
            editorLog << "screenshot: unavailable reason=native editor window capture not implemented in guarded smoke path\n";
        } else {
            editorLog << "screenshot: blocked path=" << options.screenshotPath.string() << " reason=native editor unavailable; no fake PNG written\n";
        }
    }

    std::ostringstream noEditorLog;
    noEditorLog << "vst3-editor-smoke-test: no-editor-generic\n"
                << "configured=" << (noEditorConfigured.ok() ? 1 : 0)
                << " backend=" << plugins::toString(noEditorHost.status().backend)
                << " juce=" << (noEditorHost.status().juceAvailable ? 1 : 0) << '\n'
                << "open-requested=1 native-editor-open=" << (noEditorOpen.open ? 1 : 0)
                << " native-editor-available=" << (noEditorOpen.nativeEditorAvailable ? 1 : 0)
                << " generic-parameters=" << (noEditorOpen.genericParameterSurfaceAvailable ? 1 : 0)
                << " status=" << noEditorOpen.statusText << '\n'
                << "model-component=" << noEditorModel.componentName
                << " slots=" << noEditorModel.slots.size()
                << " parameters=" << (noEditorModel.slots.empty() ? 0U : noEditorModel.slots[0].parameters.size()) << '\n'
                << "sandbox-boundary=generic-status-only future-helper-process-owned-by-task-12\n";

    const auto editorLogPath = evidencePath("task-11-editor.log");
    const auto noEditorLogPath = evidencePath("task-11-no-editor.log");
    const auto wroteEditor = writeTextFile(editorLogPath, editorLog.str());
    const auto wroteNoEditor = writeTextFile(noEditorLogPath, noEditorLog.str());

    output << editorLog.str();
    output << noEditorLog.str();
    output << "editor-log=" << editorLogPath << " wrote=" << (wroteEditor ? 1 : 0) << '\n';
    output << "no-editor-log=" << noEditorLogPath << " wrote=" << (wroteNoEditor ? 1 : 0) << '\n';
    return configured.ok() && noEditorConfigured.ok() && savedReloaded && processingPreserved && wroteEditor && wroteNoEditor ? 0 : 1;
}

}
