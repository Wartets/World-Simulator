#pragma once

// Core dependencies
#include "ws/core/runtime.hpp"

// Standard library
#include <filesystem>

namespace ws::app {

// Writes a checkpoint to a file.
void writeCheckpointFile(const RuntimeCheckpoint& checkpoint, const std::filesystem::path& path);
// Reads a checkpoint from a file.
[[nodiscard]] RuntimeCheckpoint readCheckpointFile(const std::filesystem::path& path);

} // namespace ws::app
