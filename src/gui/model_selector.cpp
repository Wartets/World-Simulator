#include "ws/gui/model_selector.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ws::gui {

namespace {

constexpr const char* kCurrentEngineVersion = "1.0.0";

enum TableColumnId {
    ColName = 0,
    ColId,
    ColVersion,
    ColFormatVersion,
    ColMinimumEngineVersion,
    ColAuthor,
    ColCreationDate,
    ColTags,
    ColDescription,
    ColCompatibility,
    ColIdentityHash,
    ColLastModified
};

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

fs::path resolveWorkspaceRoot() {
    std::error_code ec;
    fs::path current = fs::current_path(ec);
    if (ec) {
        return fs::path{"."};
    }

    for (fs::path probe = current; !probe.empty(); probe = probe.parent_path()) {
        if (fs::exists(probe / "CMakeLists.txt")) {
            return probe;
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }

    return current;
}

fs::path modelsRoot() {
    const fs::path root = resolveWorkspaceRoot() / "models";
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

std::string sanitizeModelName(const std::string& raw) {
    std::string name = raw;
    if (name.empty()) {
        return {};
    }
    for (char& c : name) {
        const bool valid =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        if (!valid) {
            c = '_';
        }
    }
    return name;
}

std::string joinTags(const std::vector<std::string>& tags) {
    if (tags.empty()) {
        return {};
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << tags[i];
    }
    return out.str();
}

std::string jsonStringOr(const json& j, const char* key, std::string fallback = {}) {
    if (!j.contains(key)) {
        return fallback;
    }
    const auto& value = j.at(key);
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float()) {
        return value.dump();
    }
    return fallback;
}

std::string compatibilityLabel(const std::string& minimumEngineVersion);

void parseMetadataInto(ModelInfo& info, const fs::path& metadataPath) {
    if (!fs::exists(metadataPath)) {
        return;
    }

    try {
        std::ifstream in(metadataPath);
        if (!in) {
            return;
        }

        json meta = json::parse(in);
        if (meta.contains("name") && meta["name"].is_string() && !meta["name"].get<std::string>().empty()) {
            info.name = meta["name"].get<std::string>();
        }
        if (meta.contains("id") && meta["id"].is_string() && !meta["id"].get<std::string>().empty()) {
            info.model_id = meta["id"].get<std::string>();
        }
        if (meta.contains("author") && meta["author"].is_string()) {
            info.author = meta["author"].get<std::string>();
        }
        if (meta.contains("creation_date") && meta["creation_date"].is_string()) {
            info.creation_date = meta["creation_date"].get<std::string>();
        }
        if (meta.contains("description") && meta["description"].is_string()) {
            info.description = meta["description"].get<std::string>();
        }
        if (meta.contains("tags") && meta["tags"].is_array()) {
            info.tags.clear();
            for (const auto& tag : meta["tags"]) {
                if (tag.is_string()) {
                    info.tags.push_back(tag.get<std::string>());
                }
            }
        }
        if (meta.contains("version") && meta["version"].is_string() && info.version == "unknown") {
            info.version = meta["version"].get<std::string>();
        }
    } catch (...) {
        // leave metadata fields as-is
    }
}

void parseVersionInto(ModelInfo& info, const fs::path& versionPath) {
    if (!fs::exists(versionPath)) {
        return;
    }

    try {
        std::ifstream in(versionPath);
        if (!in) {
            return;
        }

        json version = json::parse(in);
        if (version.contains("format_version") && version["format_version"].is_string()) {
            info.format_version = version["format_version"].get<std::string>();
        }
        if (version.contains("model_version") && version["model_version"].is_string()) {
            info.version = version["model_version"].get<std::string>();
        } else if (info.version == "unknown" && info.format_version != "unknown") {
            info.version = info.format_version;
        }
        if (version.contains("minimum_engine_version") && version["minimum_engine_version"].is_string()) {
            info.minimum_engine_version = version["minimum_engine_version"].get<std::string>();
            info.compatibility = compatibilityLabel(info.minimum_engine_version);
        }
    } catch (...) {
        // leave version fields as-is
    }
}

void parseModelJsonInto(ModelInfo& info, const fs::path& modelJsonPath) {
    if (!fs::exists(modelJsonPath)) {
        return;
    }

    try {
        std::ifstream in(modelJsonPath);
        if (!in) {
            return;
        }

        json model = json::parse(in);
        if (info.model_id.empty() && model.contains("id") && model["id"].is_string()) {
            info.model_id = model["id"].get<std::string>();
        }
        if (info.version == "unknown" && model.contains("version") && model["version"].is_string()) {
            info.version = model["version"].get<std::string>();
        }
    } catch (...) {
        // leave model fields as-is
    }
}

std::string formatFileTime(const fs::file_time_type& timePoint) {
    try {
        const auto now = std::chrono::system_clock::now();
        const auto fsNow = fs::file_time_type::clock::now();
        const auto systemTime = now + std::chrono::duration_cast<std::chrono::system_clock::duration>(timePoint - fsNow);
    const std::time_t tt = std::chrono::system_clock::to_time_t(systemTime);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    } catch (...) {
        return "n/a";
    }
}

std::string readTextFile(const fs::path& path) {
    if (!fs::exists(path)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::uint64_t fnv1a64(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

int compareVersion(const std::string& lhs, const std::string& rhs) {
    std::istringstream lss(lhs);
    std::istringstream rss(rhs);
    for (;;) {
        int lv = 0;
        int rv = 0;
        char dot = '.';
        const bool lOk = static_cast<bool>(lss >> lv);
        const bool rOk = static_cast<bool>(rss >> rv);
        if (!lOk && !rOk) {
            return 0;
        }
        if (lv != rv) {
            return lv < rv ? -1 : 1;
        }
        if (!(lss >> dot)) {
            lss.clear();
        }
        if (!(rss >> dot)) {
            rss.clear();
        }
    }
}

std::string compatibilityLabel(const std::string& minimumEngineVersion) {
    if (minimumEngineVersion.empty()) {
        return "unknown";
    }
    return compareVersion(minimumEngineVersion, kCurrentEngineVersion) <= 0 ? "compatible" : "incompatible";
}

std::string identityHashForModel(const fs::path& modelDir) {
    if (fs::is_regular_file(modelDir)) {
        const std::string payload = readTextFile(modelDir);
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(payload);
        return out.str();
    }

    const std::string payload = readTextFile(modelDir / "metadata.json") + '\n' +
        readTextFile(modelDir / "version.json") + '\n' +
        readTextFile(modelDir / "model.json") + '\n' +
        readTextFile(modelDir / "logic.ir");
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(payload);
    return out.str();
}

} // namespace

ModelSelector::ModelSelector()
    : window_open(true),
      selected_model_index(-1),
      show_new_model_dialog(false),
            show_import_dialog(false),
            show_rename_dialog(false),
            show_export_dialog(false),
            show_delete_confirm_dialog(false),
            show_column_id(true),
            show_column_version(true),
            show_column_format_version(true),
            show_column_minimum_engine_version(true),
            show_column_author(true),
            show_column_creation_date(true),
            show_column_tags(true),
            show_column_description(true),
            show_column_compatibility(true),
            show_column_identity_hash(true),
            show_column_last_modified(true),
            pending_rename_name{0},
            pending_export_path{0},
            import_source_path{0},
            import_target_name{0},
            pending_action_model_index(-1) {
    refreshModelList();
}

ModelSelector::~ModelSelector() = default;

void ModelSelector::refreshModelList() {
    const std::string selectedIdentity =
        (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size()))
            ? models[selected_model_index].identity_hash
            : std::string{};

    models.clear();

    const fs::path models_dir = modelsRoot();
    if (!fs::exists(models_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(models_dir)) {
        if ((entry.is_directory() || entry.is_regular_file()) && entry.path().extension() == ".simmodel") {
            ModelInfo info;
            info.name = entry.path().stem().string();
            info.path = entry.path().string();
            std::error_code ec;
            info.last_modified = fs::last_write_time(entry.path(), ec);
            info.identity_hash = identityHashForModel(entry.path());

            if (entry.is_directory()) {
                parseMetadataInto(info, entry.path() / "metadata.json");
                parseVersionInto(info, entry.path() / "version.json");
                parseModelJsonInto(info, entry.path() / "model.json");
            }

            if (info.model_id.empty()) {
                info.model_id = info.name;
            }
            if (info.author.empty()) {
                info.author = "n/a";
            }
            if (info.creation_date.empty()) {
                info.creation_date = "n/a";
            }
            if (info.description.empty()) {
                info.description = "n/a";
            }
            if (info.format_version == "unknown") {
                info.format_version = "n/a";
            }
            if (info.minimum_engine_version == "unknown") {
                info.minimum_engine_version = "n/a";
            }
            if (info.compatibility == "n/a" && info.minimum_engine_version != "n/a") {
                info.compatibility = compatibilityLabel(info.minimum_engine_version);
            }
            if (info.compatibility == "unknown") {
                info.compatibility = "n/a";
            }
            
            models.push_back(info);
        }
    }

    // Sort by last modified (newest first)
    std::sort(models.begin(), models.end(),
        [](const ModelInfo& a, const ModelInfo& b) {
            return a.last_modified > b.last_modified;
        });

    selected_model_index = -1;
    if (!selectedIdentity.empty()) {
        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
            if (models[i].identity_hash == selectedIdentity) {
                selected_model_index = i;
                break;
            }
        }
    }
}

void ModelSelector::render(ImVec2 available_size) {
    if (!window_open) return;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(available_size, ImGuiCond_Always);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("Model Selector Full Page", nullptr, flags)) {
        // Top action bar
        if (ImGui::Button("New Model", ImVec2(100, 0))) {
            show_new_model_dialog = true;
            ImGui::OpenPopup("New Model");
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", ImVec2(100, 0))) {
            show_import_dialog = true;
            ImGui::OpenPopup("Import Model");
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100, 0))) {
            refreshModelList();
        }
        
        ImGui::Separator();
        ImGui::TextDisabled("Data origins: model fields from metadata.json / version.json / model.json; last modified from filesystem metadata; identity hash computed from metadata.json + version.json + model.json + logic.ir.");

        const bool hasSelected = selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size());
        if (hasSelected) {
            const auto& selected = models[selected_model_index];
            ImGui::Text("Selected: %s", selected.name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (on_load_model) {
                    on_load_model(selected);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit")) {
                if (on_edit_model) on_edit_model(selected);
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate")) {
                duplicateModel(selected);
            }
            ImGui::SameLine();
            if (ImGui::Button("Rename")) {
                pending_action_model_index = selected_model_index;
                std::snprintf(pending_rename_name, sizeof(pending_rename_name), "%s", selected.name.c_str());
                show_rename_dialog = true;
                ImGui::OpenPopup("Rename Model");
            }
            ImGui::SameLine();
            if (ImGui::Button("Export")) {
                pending_action_model_index = selected_model_index;
                std::snprintf(pending_export_path, sizeof(pending_export_path), "%s_export.simmodel", selected.name.c_str());
                show_export_dialog = true;
                ImGui::OpenPopup("Export Model");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                pending_action_model_index = selected_model_index;
                show_delete_confirm_dialog = true;
                ImGui::OpenPopup("Delete Model");
            }
            ImGui::Separator();
        }
        
        // Model table
        const ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable;

        const int visible_column_count =
            1 +
            (show_column_id ? 1 : 0) +
            (show_column_version ? 1 : 0) +
            (show_column_format_version ? 1 : 0) +
            (show_column_minimum_engine_version ? 1 : 0) +
            (show_column_author ? 1 : 0) +
            (show_column_creation_date ? 1 : 0) +
            (show_column_tags ? 1 : 0) +
            (show_column_description ? 1 : 0) +
            (show_column_compatibility ? 1 : 0) +
            (show_column_identity_hash ? 1 : 0) +
            (show_column_last_modified ? 1 : 0);

        if (ImGui::BeginTable("ModelsTable", visible_column_count, tableFlags, ImVec2(0, ImGui::GetContentRegionAvail().y - 60.0f))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, ColName);
            if (show_column_id) ImGui::TableSetupColumn("ID", 0, 0.0f, ColId);
            if (show_column_version) ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_DefaultSort, 0.0f, ColVersion);
            if (show_column_format_version) ImGui::TableSetupColumn("Format", 0, 0.0f, ColFormatVersion);
            if (show_column_minimum_engine_version) ImGui::TableSetupColumn("Min Engine", 0, 0.0f, ColMinimumEngineVersion);
            if (show_column_author) ImGui::TableSetupColumn("Author", 0, 0.0f, ColAuthor);
            if (show_column_creation_date) ImGui::TableSetupColumn("Created", 0, 0.0f, ColCreationDate);
            if (show_column_tags) ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthStretch, 0.0f, ColTags);
            if (show_column_description) ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0.0f, ColDescription);
            if (show_column_compatibility) ImGui::TableSetupColumn("Compatibility", 0, 0.0f, ColCompatibility);
            if (show_column_identity_hash) ImGui::TableSetupColumn("Identity Hash", 0, 0.0f, ColIdentityHash);
            if (show_column_last_modified) ImGui::TableSetupColumn("Last Modified", 0, 0.0f, ColLastModified);
            ImGui::TableHeadersRow();

            if (ImGui::BeginPopupContextItem("ModelSelectorColumns", ImGuiPopupFlags_MouseButtonRight)) {
                ImGui::TextDisabled("Toggle table columns");
                ImGui::Separator();
                ImGui::Checkbox("ID", &show_column_id);
                ImGui::Checkbox("Version", &show_column_version);
                ImGui::Checkbox("Format version", &show_column_format_version);
                ImGui::Checkbox("Minimum engine version", &show_column_minimum_engine_version);
                ImGui::Checkbox("Author", &show_column_author);
                ImGui::Checkbox("Creation date", &show_column_creation_date);
                ImGui::Checkbox("Tags", &show_column_tags);
                ImGui::Checkbox("Description", &show_column_description);
                ImGui::Checkbox("Compatibility", &show_column_compatibility);
                ImGui::Checkbox("Identity hash", &show_column_identity_hash);
                ImGui::Checkbox("Last modified", &show_column_last_modified);
                ImGui::Separator();
                if (ImGui::MenuItem("Show all")) {
                    show_column_id = true;
                    show_column_version = true;
                    show_column_format_version = true;
                    show_column_minimum_engine_version = true;
                    show_column_author = true;
                    show_column_creation_date = true;
                    show_column_tags = true;
                    show_column_description = true;
                    show_column_compatibility = true;
                    show_column_identity_hash = true;
                    show_column_last_modified = true;
                }
                if (ImGui::MenuItem("Show compact")) {
                    show_column_id = true;
                    show_column_version = true;
                    show_column_author = true;
                    show_column_description = true;
                    show_column_identity_hash = true;
                    show_column_last_modified = true;
                    show_column_format_version = false;
                    show_column_minimum_engine_version = false;
                    show_column_tags = false;
                    show_column_compatibility = false;
                }
                ImGui::EndPopup();
            }

            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    const std::string selectedBeforeSort =
                        (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size()))
                            ? models[selected_model_index].name
                            : std::string{};
                    std::sort(models.begin(), models.end(), [&](const ModelInfo& a, const ModelInfo& b) {
                        int cmp = 0;
                        switch (spec.ColumnUserID) {
                            case ColName: cmp = toLowerCopy(a.name).compare(toLowerCopy(b.name)); break;
                            case ColId: cmp = toLowerCopy(a.model_id).compare(toLowerCopy(b.model_id)); break;
                            case ColVersion: cmp = compareVersion(a.version, b.version); break;
                            case ColFormatVersion: cmp = a.format_version.compare(b.format_version); break;
                            case ColMinimumEngineVersion: cmp = a.minimum_engine_version.compare(b.minimum_engine_version); break;
                            case ColAuthor: cmp = toLowerCopy(a.author).compare(toLowerCopy(b.author)); break;
                            case ColCreationDate: cmp = a.creation_date.compare(b.creation_date); break;
                            case ColTags: cmp = joinTags(a.tags).compare(joinTags(b.tags)); break;
                            case ColDescription: cmp = toLowerCopy(a.description).compare(toLowerCopy(b.description)); break;
                            case ColCompatibility: cmp = toLowerCopy(a.compatibility).compare(toLowerCopy(b.compatibility)); break;
                            case ColIdentityHash: cmp = a.identity_hash.compare(b.identity_hash); break;
                            case ColLastModified:
                                cmp = (a.last_modified < b.last_modified) ? -1 : (a.last_modified > b.last_modified ? 1 : 0);
                                break;
                            default:
                                cmp = 0;
                                break;
                        }
                        if (spec.SortDirection == ImGuiSortDirection_Descending) {
                            cmp = -cmp;
                        }
                        if (cmp == 0) {
                            return a.name < b.name;
                        }
                        return cmp < 0;
                    });

                    if (!selectedBeforeSort.empty()) {
                        for (int i = 0; i < static_cast<int>(models.size()); ++i) {
                            if (models[i].name == selectedBeforeSort) {
                                selected_model_index = i;
                                break;
                            }
                        }
                    }

                    sortSpecs->SpecsDirty = false;
                }
            }
            
            for (int i = 0; i < static_cast<int>(models.size()); ++i) {
                const auto& model = models[i];
                ImGui::TableNextRow();
                
                // Name column
                ImGui::TableNextColumn();
                const bool row_selected = selected_model_index == i;
                if (ImGui::Selectable(model.name.c_str(), row_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_model_index = i;
                    if (ImGui::IsMouseDoubleClicked(0) && on_edit_model) {
                        on_edit_model(model);
                    }
                }
                if (row_selected || ImGui::IsItemHovered()) {
                    ImU32 bg = row_selected ? IM_COL32(64, 128, 255, 96) : IM_COL32(96, 96, 128, 64);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
                }

                // ID column
                if (show_column_id) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.model_id.c_str()); }
                
                // Version column
                if (show_column_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.version.c_str()); }

                // Format version column
                if (show_column_format_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.format_version.c_str()); }

                // Min engine column
                if (show_column_minimum_engine_version) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.minimum_engine_version.c_str()); }

                // Author column
                if (show_column_author) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.author.c_str()); }

                // Created column
                if (show_column_creation_date) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.creation_date.c_str()); }

                // Tags column
                if (show_column_tags) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", joinTags(model.tags).c_str()); }

                // Description column
                if (show_column_description) { ImGui::TableNextColumn(); ImGui::TextWrapped("%s", model.description.c_str()); }
                
                // Compatibility column
                if (show_column_compatibility) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.compatibility.c_str()); }
                
                // Identity hash column
                if (show_column_identity_hash) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", model.identity_hash.c_str()); }

                // Last modified column
                if (show_column_last_modified) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", formatFileTime(model.last_modified).c_str()); }
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        
        // Model details (if selected)
        if (selected_model_index >= 0 && selected_model_index < static_cast<int>(models.size())) {
            ImGui::TextDisabled("Path: %s", models[selected_model_index].path.c_str());
        }
        
        // Dialogs
        if (show_new_model_dialog) {
            renderNewModelDialog();
        }
        if (show_import_dialog) {
            renderImportDialog();
        }
        if (show_rename_dialog) {
            renderRenameDialog();
        }
        if (show_export_dialog) {
            renderExportDialog();
        }
        if (show_delete_confirm_dialog) {
            renderDeleteConfirmDialog();
        }
        
        ImGui::End();
    }
}

void ModelSelector::renderNewModelDialog() {
    if (ImGui::BeginPopupModal("New Model", &show_new_model_dialog)) {
        static char model_name_buffer[256] = "MyModel";
        ImGui::InputText("Model Name", model_name_buffer, IM_ARRAYSIZE(model_name_buffer));
        
        ImGui::Separator();
        
        ImGui::Text("Choose template:");
        if (ImGui::Selectable("Blank (2D)", false)) {
            createModelFromTemplate("blank_2d", model_name_buffer);
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Advection-Diffusion", false)) {
            createModelFromTemplate("advection_diffusion", model_name_buffer);
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Cancel")) {
            show_new_model_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderImportDialog() {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 220.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import Model", &show_import_dialog)) {
        ImGui::TextWrapped("Import a .simmodel directory or archive into this workspace.");
        ImGui::InputText("Source path", import_source_path, IM_ARRAYSIZE(import_source_path));
        ImGui::InputText("Model name (optional)", import_target_name, IM_ARRAYSIZE(import_target_name));

        if (ImGui::Button("Import")) {
            const fs::path source = import_source_path;
            if (!source.empty() && fs::exists(source)) {
                std::string targetName = sanitizeModelName(import_target_name);
                fs::path destination;
                if (!targetName.empty()) {
                    destination = modelsRoot() / (targetName + ".simmodel");
                } else {
                    destination = modelsRoot() / source.filename();
                    if (destination.extension() != ".simmodel") {
                        destination += ".simmodel";
                    }
                }

                std::error_code ec;
                if (fs::exists(destination, ec)) {
                    if (fs::is_directory(destination, ec)) {
                        fs::remove_all(destination, ec);
                    } else {
                        fs::remove(destination, ec);
                    }
                }

                if (fs::is_directory(source)) {
                    fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                } else {
                    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
                }

                if (!ec) {
                    refreshModelList();
                    std::memset(import_source_path, 0, sizeof(import_source_path));
                    std::memset(import_target_name, 0, sizeof(import_target_name));
                    show_import_dialog = false;
                    ImGui::CloseCurrentPopup();
                    if (on_model_created) {
                        on_model_created(destination.string());
                    }
                }
            }
        }
        if (ImGui::Button("Cancel")) {
            show_import_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderRenameDialog() {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 190.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename Model", &show_rename_dialog)) {
        ImGui::TextWrapped("Rename the selected model. The on-disk folder and embedded metadata will be updated.");
        ImGui::InputText("New name", pending_rename_name, IM_ARRAYSIZE(pending_rename_name));

        if (ImGui::Button("Rename")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                renameModel(models[pending_action_model_index], pending_rename_name);
                show_rename_dialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_rename_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderExportDialog() {
    ImGui::SetNextWindowSize(ImVec2(840.0f, 220.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export Model", &show_export_dialog)) {
        ImGui::TextWrapped("Export the selected model to another .simmodel path. The destination will be overwritten if it already exists.");
        ImGui::InputText("Destination", pending_export_path, IM_ARRAYSIZE(pending_export_path));

        if (ImGui::Button("Export")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                exportModel(models[pending_action_model_index], pending_export_path);
                show_export_dialog = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_export_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::renderDeleteConfirmDialog() {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 130.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete Model", &show_delete_confirm_dialog)) {
        if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
            ImGui::TextWrapped("Delete '%s'? This cannot be undone.", models[pending_action_model_index].name.c_str());
        } else {
            ImGui::TextWrapped("Delete selected model?");
        }

        if (ImGui::Button("Delete")) {
            if (pending_action_model_index >= 0 && pending_action_model_index < static_cast<int>(models.size())) {
                deleteModel(models[pending_action_model_index]);
                selected_model_index = -1;
            }
            show_delete_confirm_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            show_delete_confirm_dialog = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::createModelFromTemplate(const std::string& template_name, const std::string& model_name) {
    const std::string safeName = sanitizeModelName(model_name);
    if (safeName.empty()) {
        return;
    }

    const fs::path model_path = modelsRoot() / (safeName + ".simmodel");
    std::error_code ec;
    if (fs::exists(model_path, ec)) {
        fs::remove_all(model_path, ec);
    }
    fs::create_directories(model_path, ec);

    fs::path template_source = modelsRoot() / "environmental_model_2d.simmodel";
    if (template_name == "advection_diffusion") {
        template_source = modelsRoot() / "gray_scott_reaction_diffusion.simmodel";
    }
    if (!fs::exists(template_source)) {
        template_source = resolveWorkspaceRoot() / "models" / "environmental_model_2d.simmodel";
    }
    if (fs::exists(template_source) && fs::is_directory(template_source)) {
        fs::copy(template_source, model_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }

    // Preserve template metadata when present; only update identity fields.
    json metadata = json::object();
    try {
        std::ifstream inMeta(model_path / "metadata.json");
        if (inMeta) {
            metadata = json::parse(inMeta);
        }
    } catch (...) {
        metadata = json::object();
    }
    metadata["name"] = safeName;
    if (!metadata.contains("id")) {
        metadata["id"] = safeName;
    }
    if (!metadata.contains("description")) {
        metadata["description"] = "Created from template: " + template_name;
    }
    std::ofstream mf(model_path / "metadata.json");
    mf << metadata.dump(2);
    mf.close();

    json version = json::object();
    try {
        std::ifstream inVersion(model_path / "version.json");
        if (inVersion) {
            version = json::parse(inVersion);
        }
    } catch (...) {
        version = json::object();
    }
    if (!version.contains("format_version")) {
        version["format_version"] = "1.0.0";
    }
    if (!version.contains("model_version")) {
        if (metadata.contains("version") && metadata["version"].is_string()) {
            version["model_version"] = metadata["version"].get<std::string>();
        } else {
            version["model_version"] = "1.0.0";
        }
    }
    if (!version.contains("minimum_engine_version")) {
        version["minimum_engine_version"] = kCurrentEngineVersion;
    }
    std::ofstream vf(model_path / "version.json");
    vf << version.dump(2);
    vf.close();

    json model;
    model["id"] = safeName;
    model["version"] = "1.0.0";
    model["grid"] = json::object();
    model["variables"] = json::array();
    model["stages"] = json::array();

    try {
        std::ifstream tmf(template_source / "model.json");
        if (tmf) {
            model = json::parse(tmf);
            model["id"] = safeName;
            model["version"] = "1.0.0";
        }
    } catch (...) {
        // Keep minimal fallback
    }

    std::ofstream mdf(model_path / "model.json");
    mdf << model.dump(2);
    mdf.close();

    if (!fs::exists(model_path / "logic.ir")) {
        std::ofstream ir(model_path / "logic.ir");
        ir << "// template\n";
    }
    
    refreshModelList();
    if (on_model_created) {
        on_model_created(model_path.string());
    }
}

void ws::gui::ModelSelector::duplicateModel(const ws::gui::ModelInfo& model) {
    int counter = 1;
    fs::path dst_path;
    do {
        const std::string suffix = (counter == 1) ? "_copy" : ("_copy_" + std::to_string(counter));
        dst_path = modelsRoot() / (model.name + suffix + ".simmodel");
        ++counter;
    } while (fs::exists(dst_path));

    const fs::path src_path = model.path;
    std::error_code ec;
    if (fs::is_directory(src_path, ec)) {
        fs::copy(src_path, dst_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(src_path, dst_path, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        return;
    }

    const std::string new_name = dst_path.stem().string();
    try {
        const fs::path metadataPath = dst_path / "metadata.json";
        if (fs::exists(metadataPath)) {
            std::ifstream in(metadataPath);
            json metadata = json::parse(in);
            metadata["name"] = new_name;
            if (metadata.contains("id") && metadata["id"].is_string()) {
                metadata["id"] = new_name;
            }
            std::ofstream out(metadataPath, std::ios::trunc);
            out << metadata.dump(2);
        }
    } catch (...) {
        // keep duplicate best effort
    }
    
    refreshModelList();
}

void ws::gui::ModelSelector::renameModel(const ws::gui::ModelInfo& model, const std::string& new_name) {
    const std::string safeName = sanitizeModelName(new_name);
    if (safeName.empty()) {
        return;
    }

    const fs::path source = model.path;
    const fs::path destination = modelsRoot() / (safeName + ".simmodel");
    if (source == destination) {
        return;
    }

    std::error_code ec;
    if (fs::exists(destination, ec)) {
        return;
    }

    fs::rename(source, destination, ec);
    if (ec) {
        return;
    }

    try {
        const fs::path meta = destination / "metadata.json";
        if (fs::exists(meta)) {
            std::ifstream in(meta);
            json mj = json::parse(in);
            mj["name"] = safeName;
            std::ofstream out(meta, std::ios::trunc);
            out << mj.dump(2);
        }
    } catch (...) {
    }

    try {
        const fs::path modelJson = destination / "model.json";
        if (fs::exists(modelJson)) {
            std::ifstream in(modelJson);
            json mj = json::parse(in);
            mj["id"] = safeName;
            std::ofstream out(modelJson, std::ios::trunc);
            out << mj.dump(2);
        }
    } catch (...) {
    }

    refreshModelList();
}

void ws::gui::ModelSelector::exportModel(const ws::gui::ModelInfo& model, const std::filesystem::path& destination) {
    if (destination.empty()) {
        return;
    }

    fs::path target = destination;
    if (target.extension() != ".simmodel") {
        target += ".simmodel";
    }

    std::error_code ec;
    if (fs::exists(target, ec)) {
        if (fs::is_directory(target, ec)) {
            fs::remove_all(target, ec);
        } else {
            fs::remove(target, ec);
        }
    }

    if (fs::is_directory(model.path, ec)) {
        fs::copy(model.path, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(model.path, target, fs::copy_options::overwrite_existing, ec);
    }
}

void ws::gui::ModelSelector::deleteModel(const ws::gui::ModelInfo& model) {
    std::error_code ec;
    if (fs::is_directory(model.path, ec)) {
        fs::remove_all(model.path, ec);
    } else {
        fs::remove(model.path, ec);
    }
    refreshModelList();
}

} // namespace ws::gui
