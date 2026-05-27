#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

namespace djapp::audio {

struct AudioDeckSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/dj-workflow"};
    std::filesystem::path renderPath{};
    std::string chain{"deck-a"};
};

[[nodiscard]] int runAudioDeckSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options);
[[nodiscard]] int runTempoSyncSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options);
[[nodiscard]] int runTimeStretchOverloadSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options);
[[nodiscard]] int runMixerSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options);
[[nodiscard]] int runVst3ProcessingSmokeTest(std::ostream& output, const AudioDeckSmokeOptions& options);

}
