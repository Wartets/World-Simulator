#pragma once

#include "ws/core/event_queue.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

// Saves manual event log to a file.
[[nodiscard]] bool saveManualEventLog(
    const std::vector<ManualEventRecord>& events,
    const std::filesystem::path& outputPath,
    std::string& message);

// Loads manual event log from a file.
[[nodiscard]] bool loadManualEventLog(
    const std::filesystem::path& inputPath,
    std::vector<ManualEventRecord>& events,
    std::string& message);

} // namespace ws::gui
