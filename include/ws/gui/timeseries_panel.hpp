#pragma once

#include "ws/core/probe.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui {

[[nodiscard]] std::string probeKindToString(ProbeKind kind);
[[nodiscard]] bool saveProbeSeriesCsv(const std::vector<ProbeSeries>& series, const std::filesystem::path& outputPath, std::string& message);

} // namespace ws::gui
