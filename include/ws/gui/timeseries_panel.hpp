#pragma once

#include "ws/core/probe.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

// Converts a probe kind to a human-readable string.
[[nodiscard]] std::string probeKindToString(ProbeKind kind);
// Saves probe time series data to a CSV file.
[[nodiscard]] bool saveProbeSeriesCsv(const std::vector<ProbeSeries>& series, const std::filesystem::path& outputPath, std::string& message);

} // namespace ws::gui
