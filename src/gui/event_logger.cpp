#include "ws/gui/event_logger.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>

namespace ws::gui {

namespace {

std::string kindToString(const ManualEventKind kind) {
    switch (kind) {
        case ManualEventKind::ParameterUpdate: return "parameter_update";
        case ManualEventKind::CellEdit: return "cell_edit";
        case ManualEventKind::Perturbation: return "perturbation";
    }
    return "cell_edit";
}

ManualEventKind kindFromString(const std::string& value) {
    if (value == "parameter_update") {
        return ManualEventKind::ParameterUpdate;
    }
    if (value == "perturbation") {
        return ManualEventKind::Perturbation;
    }
    return ManualEventKind::CellEdit;
}

} // namespace

bool saveManualEventLog(
    const std::vector<ManualEventRecord>& events,
    const std::filesystem::path& outputPath,
    std::string& message) {
    std::error_code ec;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            message = "event_log_save_failed reason=create_directory";
            return false;
        }
    }

    nlohmann::json root;
    root["events"] = nlohmann::json::array();
    for (const auto& event : events) {
        root["events"].push_back({
            {"step", event.step},
            {"time", event.time},
            {"variable", event.variable},
            {"cell_index", event.cellIndex},
            {"old_value", event.oldValue},
            {"new_value", event.newValue},
            {"description", event.description},
            {"timestamp", event.timestamp},
            {"kind", kindToString(event.kind)}});
    }

    std::ofstream output(outputPath, std::ios::trunc);
    if (!output.is_open()) {
        message = "event_log_save_failed reason=file_open";
        return false;
    }

    output << root.dump(2);
    message = "event_log_saved path=" + outputPath.string();
    return true;
}

bool loadManualEventLog(
    const std::filesystem::path& inputPath,
    std::vector<ManualEventRecord>& events,
    std::string& message) {
    std::ifstream input(inputPath);
    if (!input.is_open()) {
        message = "event_log_load_failed reason=file_open";
        return false;
    }

    nlohmann::json root;
    input >> root;

    if (!root.contains("events") || !root["events"].is_array()) {
        message = "event_log_load_failed reason=invalid_schema";
        return false;
    }

    events.clear();
    for (const auto& item : root["events"]) {
        ManualEventRecord event;
        event.step = item.value("step", 0ull);
        event.time = item.value("time", 0.0f);
        event.variable = item.value("variable", std::string{});
        event.cellIndex = item.value("cell_index", std::numeric_limits<std::uint64_t>::max());
        event.oldValue = item.value("old_value", 0.0f);
        event.newValue = item.value("new_value", 0.0f);
        event.description = item.value("description", std::string{});
        event.timestamp = item.value("timestamp", 0ull);
        event.kind = kindFromString(item.value("kind", std::string{"cell_edit"}));
        events.push_back(std::move(event));
    }

    message = "event_log_loaded path=" + inputPath.string() + " count=" + std::to_string(events.size());
    return true;
}

} // namespace ws::gui
