#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--helper-smoke";
    std::cout << "DJAppPluginSandboxHelper: deterministic helper executable available\n";
    std::cout << "ipc=shared-memory-style-bounded-audio control=parameter-midi-transport-state heartbeat=status\n";
    return smoke ? 0 : 0;
}
