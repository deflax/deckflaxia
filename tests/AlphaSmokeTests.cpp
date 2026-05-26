#include "app/AlphaSmoke.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    if (filter != "all" && filter != "end-to-end") {
        std::cerr << "FAILED: unknown AlphaSmoke filter " << filter << '\n';
        return 1;
    }
    return djapp::app::runAlphaSmokeTest(std::cout);
}
