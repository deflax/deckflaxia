#include "app/UiShell.h"

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

} // namespace

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const bool emptyOnly = filter == "empty";

    const deckflaxia::app::HybridUiShellModel shell;
    const auto fullSnapshot = shell.buildSnapshot(deckflaxia::app::createUiSmokeInput(false));

    if (!emptyOnly) {
        if (expect(fullSnapshot.decks.size() == 4, "UI shell should expose exactly four deck panels") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.browser.componentName == "browser-library-panel", "browser component should be named") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.routing.componentName == "routing-panel", "routing component should be named") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.pluginChain.slots.size() == 20, "plugin chain should expose four slots per deck plus master") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.pluginChain.componentName == "plugin-chain-panel", "plugin chain panel should be named") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.mixer.componentName == "mixer-controls-panel", "mixer panel should be named") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.midiLearn.componentName == "midi-learn-indicator" && fullSnapshot.midiLearn.mappingCount > 0U,
                   "MIDI learn indicator should expose registry snapshot") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.status.componentName == "app-status-errors", "status/errors panel should be named") != 0) {
            return 1;
        }
        if (expect(fullSnapshot.pluginChain.slots[0].componentName == "plugin-slot-deck-1-1" && fullSnapshot.pluginChain.slots[0].placeholder,
                   "first plugin slot should represent no-plugin placeholder state") != 0) {
            return 1;
        }
        if (expect(!fullSnapshot.pluginChain.slots[0].nativeEditorAvailable,
                   "placeholder plugin slot should represent unavailable native editor honestly") != 0) {
            return 1;
        }
        if (expect(!fullSnapshot.decks[0].waveform.placeholder, "first smoke deck should render waveform summary") != 0) {
            return 1;
        }
    }

    const auto emptySnapshot = shell.buildSnapshot(deckflaxia::app::createUiSmokeInput(true));
    if (expect(emptySnapshot.browser.empty, "empty library should produce browser empty state") != 0) {
        return 1;
    }
    if (expect(emptySnapshot.browser.tracks.empty(), "empty library should expose zero browser tracks") != 0) {
        return 1;
    }
    if (expect(emptySnapshot.decks[0].waveform.placeholder && emptySnapshot.decks[3].waveform.placeholder,
               "missing waveform data should render placeholders") != 0) {
        return 1;
    }

    const auto report = shell.formatSmokeReport(emptyOnly ? emptySnapshot : fullSnapshot);
    if (expect(report.find("deck-panels: 4") != std::string::npos, "smoke report should include deck count") != 0) {
        return 1;
    }
    if (expect(report.find("plugin-chain-panel") != std::string::npos, "smoke report should include plugin chain") != 0) {
        return 1;
    }

    std::cout << "UiShell tests passed\n";
    return 0;
}
