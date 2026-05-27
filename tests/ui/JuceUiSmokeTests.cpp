#include "app/UiShell.h"
#include "ui/JuceComponentTree.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int testSnapshotContract() {
    const djapp::app::HybridUiShellModel shell;
    const auto snapshot = shell.buildSnapshot(djapp::app::createUiSmokeInput(false));
    if (expect(snapshot.decks.size() == 4U, "JUCE UI snapshot bridge should expose four decks") != 0) {
        return 1;
    }
    if (expect(snapshot.browser.componentName == "browser-library-panel", "browser snapshot should be present") != 0) {
        return 1;
    }
    if (expect(snapshot.pluginChain.slots.size() == 20U, "plugin chain snapshot should expose four slots per deck plus master") != 0) {
        return 1;
    }
    if (expect(snapshot.midiLearn.mappingCount > 0U, "MIDI learn snapshot should bind registry state") != 0) {
        return 1;
    }
    if (expect(snapshot.status.ok, "status snapshot should be healthy") != 0) {
        return 1;
    }
    std::cout << "JuceUi.SnapshotContract decks=" << snapshot.decks.size()
              << " plugin-slots=" << snapshot.pluginChain.slots.size() << '\n';
    return 0;
}

int testUnavailableSurface() {
    std::ostringstream output;
    const auto result = djapp::ui::runUnavailableJuceUiSmoke(output, true, {});
    const auto text = output.str();
    if (expect(result == 0, "unavailable dump surface should document blocker without failing fallback CTest") != 0) {
        return 1;
    }
    if (expect(text.find("juce-ui-smoke-test: blocked") != std::string::npos, "unavailable surface should report blocked") != 0) {
        return 1;
    }
    if (expect(text.find("dump-components: blocked") != std::string::npos, "unavailable dump should not fake a tree") != 0) {
        return 1;
    }

    std::ostringstream screenshotOutput;
    const auto screenshotResult = djapp::ui::runUnavailableJuceUiSmoke(screenshotOutput, false, ".omo/evidence/real-playable-juce/task-6-ui.png");
    if (expect(screenshotResult != 0, "unavailable screenshot should fail rather than fake PNG success") != 0) {
        return 1;
    }
    if (expect(screenshotOutput.str().find("screenshot: blocked") != std::string::npos, "unavailable screenshot should document blocker") != 0) {
        return 1;
    }
    std::cout << "JuceUi.UnavailableSurface documented=1\n";
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    if (filter == "snapshot") {
        return testSnapshotContract();
    }
    if (filter == "unavailable") {
        return testUnavailableSurface();
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown JuceUi filter " << filter << '\n';
        return 1;
    }
    if (testSnapshotContract() != 0 || testUnavailableSurface() != 0) {
        return 1;
    }
    std::cout << "Juce UI smoke tests passed\n";
    return 0;
}
