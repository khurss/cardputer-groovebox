#pragma once

#include <cstddef>
#include <cstdint>

#include "gb/model.h"

namespace gb {

constexpr size_t kProjectBufferSize = 36 * 1024;

enum class LoadResult : uint8_t {
    Ok,
    OkFromBackup,  // The main file was damaged and the backup was loaded.
    NotFound,
    ReadError,  // The file exists but could not be opened.
    BadMagic,
    BadVersion,
    BadSize,
    BadChecksum,
    Invalid
};

// Writes the versioned file image. Returns its size, or 0 if the buffer is too small.
size_t serializeProject(const Project& project, uint8_t* buffer, size_t capacity);

// Fills `out` only when the whole image is good.
LoadResult deserializeProject(const uint8_t* data, size_t size, Project& out);

// Writes through a temporary file and keeps the previous save as `<path>.bak`.
bool saveProjectFile(const char* path, const Project& project, uint8_t* buffer, size_t capacity);

// Reads `path`, falling back to `<path>.bak` if it is missing or damaged.
LoadResult loadProjectFile(const char* path, Project& out, uint8_t* buffer, size_t capacity);

}  // namespace gb
