#include "library/AudioImport.h"

#include <algorithm>
#include <cctype>

namespace djapp::library {
namespace {

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool hasSuffix(const std::string& value, const char* suffix) noexcept {
    const std::string wanted{suffix};
    return value.size() >= wanted.size() && value.compare(value.size() - wanted.size(), wanted.size(), wanted) == 0;
}

std::string formatForPath(const std::string& lowerPath) {
    if (hasSuffix(lowerPath, ".wav")) {
        return "wav";
    }
    if (hasSuffix(lowerPath, ".aiff") || hasSuffix(lowerPath, ".aif")) {
        return "aiff";
    }
    if (hasSuffix(lowerPath, ".flac")) {
        return "flac";
    }
    if (hasSuffix(lowerPath, ".mp3")) {
        return "mp3";
    }
    return {};
}

}

AudioImportClassification classifyAudioImport(const FilesystemEntry& entry) noexcept {
    AudioImportClassification classification{entry, {}, AudioImportError::None};
    if (!entry.regularFile) {
        classification.error = AudioImportError::NotRegularFile;
        return classification;
    }

    const auto lowerPath = lowerCopy(entry.path);
    classification.format = formatForPath(lowerPath);
    if (classification.format.empty()) {
        classification.error = AudioImportError::UnsupportedFormat;
        return classification;
    }
    if (lowerPath.find("corrupt") != std::string::npos) {
        classification.error = AudioImportError::CorruptAudio;
        return classification;
    }
#if !DJAPP_HAS_JUCE
    if (classification.format == "mp3") {
        classification.error = AudioImportError::ExternalToolRequired;
    }
#endif
    return classification;
}

std::vector<AudioImportClassification> classifyAudioImports(const std::vector<FilesystemEntry>& entries) {
    std::vector<AudioImportClassification> results;
    results.reserve(entries.size());
    for (const auto& entry : entries) {
        results.push_back(classifyAudioImport(entry));
    }
    return results;
}

const char* toString(AudioImportError error) noexcept {
    switch (error) {
    case AudioImportError::None:
        return "none";
    case AudioImportError::NotRegularFile:
        return "not-regular-file";
    case AudioImportError::UnsupportedFormat:
        return "unsupported-format";
    case AudioImportError::CorruptAudio:
        return "corrupt-audio";
    case AudioImportError::ExternalToolRequired:
        return "external-tool-required";
    }
    return "unknown";
}

}
