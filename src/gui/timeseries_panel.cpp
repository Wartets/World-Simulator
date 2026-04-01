#include "ws/gui/timeseries_panel.hpp"

#include <fstream>

namespace ws::gui {

std::string probeKindToString(const ProbeKind kind) {
    switch (kind) {
        case ProbeKind::GlobalScalar:
            return "global";
        case ProbeKind::CellScalar:
            return "cell";
        case ProbeKind::RegionAverage:
            return "region_avg";
    }
    return "unknown";
}

bool saveProbeSeriesCsv(const std::vector<ProbeSeries>& series, const std::filesystem::path& outputPath, std::string& message) {
    std::error_code ec;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            message = "probe_csv_export_failed reason=create_directory";
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::trunc);
    if (!output.is_open()) {
        message = "probe_csv_export_failed reason=file_open";
        return false;
    }

    output << "probe_id,variable,kind,step,time,value\n";
    for (const auto& probe : series) {
        const auto kindText = probeKindToString(probe.definition.kind);
        for (const auto& sample : probe.samples) {
            output << probe.definition.id << ','
                   << probe.definition.variableName << ','
                   << kindText << ','
                   << sample.step << ','
                   << sample.time << ','
                   << sample.value
                   << '\n';
        }
    }

    message = "probe_csv_exported path=" + outputPath.string();
    return true;
}

} // namespace ws::gui
