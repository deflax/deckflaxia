#pragma once

#include "core/DomainModels.h"
#include "plugins/PluginChainProcessor.h"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace deckflaxia::ui {

enum class PluginEditorSurfaceKind : unsigned char {
    NativeSeparateWindow,
    GenericParameterSurface,
    NativeEditorUnavailable,
};

struct PluginParameterViewModel final {
    std::string componentName;
    std::string displayName;
    std::string identifier;
    double normalizedValue{};
};

struct PluginSlotEditorViewModel final {
    std::string componentName;
    std::string displayName;
    std::string targetLabel;
    std::size_t slotIndex{};
    bool bypassed{};
    bool removable{};
    bool canMoveUp{};
    bool canMoveDown{};
    PluginEditorSurfaceKind surface{PluginEditorSurfaceKind::GenericParameterSurface};
    std::string processingStatus;
    std::string boundaryStatus;
    std::vector<PluginParameterViewModel> parameters;
    std::vector<std::string> controls;
};

struct PluginChainEditorViewModel final {
    std::string componentName{"vst3-editor-chain-panel"};
    std::string targetLabel;
    std::vector<PluginSlotEditorViewModel> slots;
    std::string stateStatus;
    std::string sandboxStatus;
    std::string nativeEditorStatus;
};

[[nodiscard]] PluginChainEditorViewModel buildPluginChainEditorModel(plugins::PluginChainTargetKind target,
                                                                     const core::PluginChainDescriptor& chain,
                                                                     const plugins::PluginProcessingStatus& status);
[[nodiscard]] bool setPluginSlotBypass(core::PluginChainDescriptor& chain, std::size_t slotIndex, bool bypassed) noexcept;
[[nodiscard]] bool removePluginSlot(core::PluginChainDescriptor& chain, std::size_t slotIndex) noexcept;
[[nodiscard]] bool movePluginSlot(core::PluginChainDescriptor& chain, std::size_t fromSlot, std::size_t toSlot) noexcept;
[[nodiscard]] bool setPluginParameter(core::PluginChainDescriptor& chain,
                                      std::size_t slotIndex,
                                      const std::string& parameterId,
                                      double normalizedValue) noexcept;
[[nodiscard]] bool saveAndReloadPluginChain(core::PluginChainDescriptor chain, core::PluginChainDescriptor& reloaded);
[[nodiscard]] const char* toString(PluginEditorSurfaceKind surface) noexcept;

struct VST3EditorSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/plugins"};
    std::filesystem::path screenshotPath{};
};

[[nodiscard]] int runVst3EditorSmokeTest(std::ostream& output, const VST3EditorSmokeOptions& options);

}
