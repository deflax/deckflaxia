#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct FixtureSpec final {
    std::string filename;
    std::string format;
    double bpm{};
    std::uint32_t sampleRateHz{};
    std::uint32_t channels{};
    double durationSeconds{};
    bool generated{};
    std::string validation;
    std::string note;
};

enum class FixtureValidationError : std::uint8_t {
    None,
    MissingFixture,
    UnsupportedFormat,
    CorruptAudio,
};

struct FixtureValidationResult final {
    FixtureValidationError error{FixtureValidationError::None};

    [[nodiscard]] bool ok() const noexcept {
        return error == FixtureValidationError::None;
    }
};

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

bool writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const auto temporaryPath = path.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (!error) {
        return true;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        return false;
    }
    return true;
}

std::filesystem::path fixtureDirectory(int argc, char* argv[]) {
    if (argc > 2) {
        return std::filesystem::path(argv[2]);
    }
    return std::filesystem::path("tests/fixtures/dj-workflow");
}

std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void appendText(std::vector<std::uint8_t>& bytes, const std::string& text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

std::int16_t sampleForFrame(std::uint32_t frame, std::uint32_t sampleRateHz, double bpm, bool silence) {
    if (silence) {
        return 0;
    }
    const auto samplesPerBeat = static_cast<std::uint32_t>((static_cast<double>(sampleRateHz) * 60.0) / bpm);
    if (samplesPerBeat > 0U && frame % samplesPerBeat < 96U) {
        return 14000;
    }
    constexpr double twoPi = 6.28318530717958647692;
    const auto wave = std::sin(twoPi * 440.0 * static_cast<double>(frame) / static_cast<double>(sampleRateHz));
    return static_cast<std::int16_t>(wave * 2800.0);
}

std::vector<std::uint8_t> wavBytes(double bpm, double durationSeconds, bool silence) {
    constexpr std::uint32_t sampleRateHz = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    const auto frameCount = static_cast<std::uint32_t>(durationSeconds * static_cast<double>(sampleRateHz));
    const auto dataSize = frameCount * channels * (bitsPerSample / 8U);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(44U + dataSize);
    appendText(bytes, "RIFF");
    appendU32(bytes, 36U + dataSize);
    appendText(bytes, "WAVE");
    appendText(bytes, "fmt ");
    appendU32(bytes, 16);
    appendU16(bytes, 1);
    appendU16(bytes, channels);
    appendU32(bytes, sampleRateHz);
    appendU32(bytes, sampleRateHz * channels * (bitsPerSample / 8U));
    appendU16(bytes, channels * (bitsPerSample / 8U));
    appendU16(bytes, bitsPerSample);
    appendText(bytes, "data");
    appendU32(bytes, dataSize);

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto left = sampleForFrame(frame, sampleRateHz, bpm, silence);
        const auto right = static_cast<std::int16_t>(-left / 2);
        appendU16(bytes, static_cast<std::uint16_t>(left));
        appendU16(bytes, static_cast<std::uint16_t>(right));
    }
    return bytes;
}

std::vector<std::uint8_t> corruptWavBytes() {
    return {'R', 'I', 'F', 'F', 0x10, 0x00, 0x00, 0x00, 'N', 'O', 'P', 'E', 'b', 'a', 'd'};
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hashString(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << fnv1a64(bytes);
    return output.str();
}

std::vector<FixtureSpec> fixtureSpecs() {
    return {
        {"track_120bpm.wav", "wav", 120.0, 48000, 2, 2.0, true, "valid-audio", "generated sine plus deterministic beat clicks"},
        {"track_128bpm.wav", "wav", 128.0, 48000, 2, 2.0, true, "valid-audio", "generated sine plus deterministic beat clicks"},
        {"track_95bpm.mp3", "mp3", 95.0, 0, 0, 0.0, false, "external-tool-required", "MP3 fixture generation awaits an AGPL-compatible encoder/decoder decision; no fake MP3 is written"},
        {"corrupt_audio.wav", "wav", 0.0, 0, 0, 0.0, true, "corrupt-audio", "malformed RIFF header used for typed error validation"},
        {"not_audio.txt", "text", 0.0, 0, 0, 0.0, true, "unsupported-format", "non-audio fixture used for typed unsupported-format validation"},
        {"silence_10s.wav", "wav", 0.0, 48000, 2, 10.0, true, "valid-audio", "generated digital silence"},
    };
}

std::vector<std::uint8_t> bytesForSpec(const FixtureSpec& spec) {
    if (spec.filename == "corrupt_audio.wav") {
        return corruptWavBytes();
    }
    if (spec.filename == "not_audio.txt") {
        const std::string text = "not audio\n";
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }
    return wavBytes(spec.bpm, spec.durationSeconds, spec.filename == "silence_10s.wav");
}

FixtureValidationResult validateAudioFixture(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {FixtureValidationError::MissingFixture};
    }
    if (lowerExtension(path) != ".wav") {
        return {FixtureValidationError::UnsupportedFormat};
    }
    const auto bytes = readBytes(path);
    if (bytes.size() < 44U || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF" ||
        std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE" || std::string(bytes.begin() + 12, bytes.begin() + 16) != "fmt " ||
        std::string(bytes.begin() + 36, bytes.begin() + 40) != "data") {
        return {FixtureValidationError::CorruptAudio};
    }
    return {};
}

std::string manifestForDirectory(const std::filesystem::path& directory) {
    std::ostringstream output;
    output << "{\n";
    output << "  \"schema\": \"djapp-fixtures-v1\",\n";
    output << "  \"directory\": \"tests/fixtures/dj-workflow\",\n";
    output << "  \"hash_algorithm\": \"fnv1a64\",\n";
    output << "  \"fixtures\": [\n";
    const auto specs = fixtureSpecs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto& spec = specs[index];
        const auto hash = spec.generated ? hashString(readBytes(directory / spec.filename)) : std::string("external-tool-required");
        output << "    {\n";
        output << "      \"file\": \"" << spec.filename << "\",\n";
        output << "      \"format\": \"" << spec.format << "\",\n";
        output << "      \"bpm\": " << std::fixed << std::setprecision(1) << spec.bpm << ",\n";
        output << "      \"sample_rate_hz\": " << spec.sampleRateHz << ",\n";
        output << "      \"channels\": " << spec.channels << ",\n";
        output << "      \"duration_seconds\": " << std::fixed << std::setprecision(1) << spec.durationSeconds << ",\n";
        output << "      \"generated\": " << (spec.generated ? "true" : "false") << ",\n";
        output << "      \"hash\": \"" << hash << "\",\n";
        output << "      \"validation\": \"" << spec.validation << "\",\n";
        output << "      \"note\": \"" << spec.note << "\"\n";
        output << "    }" << (index + 1U == specs.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"helpers\": {\n";
    output << "    \"screenshots\": \"write PNG captures as .omo/evidence/real-playable-juce/task-{N}-{slug}.png\",\n";
    output << "    \"rendered_audio_diffs\": \"write rendered WAVs as task-{N}-{slug}.wav and JSON metrics as task-{N}-{slug}.json\",\n";
    output << "    \"sqlite_assertions\": \"write task database snapshots as task-{N}-{slug}.db and assertion logs as task-{N}-{slug}.log\",\n";
    output << "    \"plugin_fixtures\": \"place generated AGPL-compatible test plugins under tests/fixtures/plugins with their own manifest.json\"\n";
    output << "  }\n";
    output << "}\n";
    return output.str();
}

void writeManifest(const std::filesystem::path& directory) {
    std::ofstream output(directory / "manifest.json", std::ios::trunc);
    output << manifestForDirectory(directory);
}

int materializeGeneratedFixtures(const std::filesystem::path& directory) {
    for (const auto& spec : fixtureSpecs()) {
        if (spec.generated) {
            const auto expectedBytes = bytesForSpec(spec);
            const auto path = directory / spec.filename;
            if (expect(writeBytes(path, expectedBytes), spec.filename + " should be rewritable") != 0) {
                return 1;
            }
            if (expect(std::filesystem::exists(path), spec.filename + " should exist before manifest hashing") != 0) {
                return 1;
            }
            if (expect(readBytes(path) == expectedBytes, spec.filename + " should match deterministic generated bytes") != 0) {
                return 1;
            }
        }
    }
    return 0;
}

std::string hashManifestText(const std::string& manifest) {
    return hashString(std::vector<std::uint8_t>(manifest.begin(), manifest.end()));
}

int generateFixtures(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    if (materializeGeneratedFixtures(directory) != 0) {
        return 1;
    }
    const auto manifestBefore = manifestForDirectory(directory);
    if (materializeGeneratedFixtures(directory) != 0) {
        return 1;
    }
    const auto manifestAfter = manifestForDirectory(directory);
    if (expect(hashManifestText(manifestBefore) == hashManifestText(manifestAfter), "fixture manifest hash should remain stable across generation") != 0) {
        return 1;
    }

    writeManifest(directory);

    std::cout << "Fixtures.Generate manifest=" << hashManifestText(manifestAfter) << '\n';
    for (const auto& spec : fixtureSpecs()) {
        std::cout << "fixture " << spec.filename << " validation=" << spec.validation;
        if (spec.generated) {
            std::cout << " hash=" << hashString(readBytes(directory / spec.filename));
        }
        std::cout << '\n';
    }
    return 0;
}

int testCorruptAudio(const std::filesystem::path& directory) {
    if (generateFixtures(directory) != 0) {
        return 1;
    }
    const auto result = validateAudioFixture(directory / "corrupt_audio.wav");
    if (expect(!result.ok(), "corrupt fixture should fail validation") != 0) {
        return 1;
    }
    if (expect(result.error == FixtureValidationError::CorruptAudio, "corrupt fixture should return typed corrupt-audio error") != 0) {
        return 1;
    }
    const auto unsupported = validateAudioFixture(directory / "not_audio.txt");
    if (expect(!unsupported.ok(), "non-audio fixture should fail validation") != 0) {
        return 1;
    }
    if (expect(unsupported.error == FixtureValidationError::UnsupportedFormat, "non-audio fixture should return typed unsupported-format error") != 0) {
        return 1;
    }
    const auto valid = validateAudioFixture(directory / "track_120bpm.wav");
    if (expect(valid.ok(), "valid generated WAV should pass validation") != 0) {
        return 1;
    }
    std::cout << "Fixtures.CorruptAudio typed-error=corrupt-audio unsupported-format=1 no-crash=1\n";
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const auto directory = fixtureDirectory(argc, argv);

    if (filter == "generate") {
        return generateFixtures(directory);
    }
    if (filter == "corrupt") {
        return testCorruptAudio(directory);
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown Fixtures filter " << filter << '\n';
        return 1;
    }
    if (generateFixtures(directory) != 0 || testCorruptAudio(directory) != 0) {
        return 1;
    }
    std::cout << "Fixtures tests passed\n";
    return 0;
}
