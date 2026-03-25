#pragma once

#include "ws/core/runtime.hpp"

#include <filesystem>

namespace ws::app {

void writeCheckpointFile(const RuntimeCheckpoint& checkpoint, const std::filesystem::path& path);
[[nodiscard]] RuntimeCheckpoint readCheckpointFile(const std::filesystem::path& path);

} // namespace ws::app
