#pragma once

#include "library/LibraryModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deckflaxia::library {

enum class AudioImportError : std::uint8_t {
    None,
    NotRegularFile,
    UnsupportedFormat,
    CorruptAudio,
    ExternalToolRequired,
};

struct AudioImportClassification final {
    FilesystemEntry entry;
    std::string format;
    AudioImportError error{AudioImportError::None};

    [[nodiscard]] bool importable() const noexcept { return error == AudioImportError::None; }
};

[[nodiscard]] AudioImportClassification classifyAudioImport(const FilesystemEntry& entry) noexcept;
[[nodiscard]] std::vector<AudioImportClassification> classifyAudioImports(const std::vector<FilesystemEntry>& entries);
[[nodiscard]] const char* toString(AudioImportError error) noexcept;

}
