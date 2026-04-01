#include "ws/gui/model_selector.hpp"

#include <imgui.h>
#include <algorithm>
#include <chrono>
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
      show_import_dialog(false) {
    refreshModelList();
}

ModelSelector::~ModelSelector() = default;

void ModelSelector::refreshModelList() {
    models.clear();
    
    const std::string models_dir = "models";
    if (!fs::exists(models_dir)) {
        fs::create_directories(models_dir);
        return;
    }
    
    for (const auto& entry : fs::directory_iterator(models_dir)) {
        if (entry.is_directory() && entry.path().extension() == ".simmodel") {
            ModelInfo info;
            info.name = entry.path().stem().string();
            info.path = entry.path().string();
            info.last_modified = fs::last_write_time(entry);
            info.identity_hash = identityHashForModel(entry.path());
            
            // Try to load version info
            std::string version_path = (entry.path() / "version.json").string();
            if (fs::exists(version_path)) {
                try {
                    std::ifstream vf(version_path);
                    json vj = json::parse(vf);
                    if (vj.contains("model_version")) {
                        info.version = vj["model_version"].get<std::string>();
                    }
                    if (vj.contains("minimum_engine_version")) {
                        info.compatibility = compatibilityLabel(vj["minimum_engine_version"].get<std::string>());
                    }
                } catch (...) {
                    info.version = "unknown";
                    info.compatibility = "unknown";
                }
            }
            if (info.compatibility == "unknown") {
                info.compatibility = "compatible";
            }
            
            models.push_back(info);
        }
    }
    
    // Sort by last modified (newest first)
    std::sort(models.begin(), models.end(),
        [](const ModelInfo& a, const ModelInfo& b) {
            return a.last_modified > b.last_modified;
        });
}

void ModelSelector::render(ImVec2 available_size) {
    if (!window_open) return;
    
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(available_size, ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Model Selector", &window_open)) {
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
        
        // Model table
        if (ImGui::BeginTable("ModelsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Version");
            ImGui::TableSetupColumn("Last Modified");
            ImGui::TableSetupColumn("Compatibility");
            ImGui::TableSetupColumn("Identity Hash");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();
            
            for (int i = 0; i < static_cast<int>(models.size()); ++i) {
                const auto& model = models[i];
                ImGui::TableNextRow();
                
                // Name column
                ImGui::TableNextColumn();
                if (ImGui::Selectable(model.name.c_str(), selected_model_index == i)) {
                    selected_model_index = i;
                }
                
                // Version column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", model.version.c_str());
                
                // Last Modified column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", formatFileTime(model.last_modified).c_str());
                
                // Compatibility column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", model.compatibility.c_str());

                // Identity hash column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", model.identity_hash.c_str());
                
                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::Button(("Load##" + std::to_string(i)).c_str(), ImVec2(44, 0))) {
                    on_edit_model(model);
                }
                ImGui::SameLine();
                if (ImGui::Button(("Edit##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    on_edit_model(model);
                }
                ImGui::SameLine();
                if (ImGui::Button(("Dup##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    duplicateModel(model);
                }
                ImGui::SameLine();
                if (ImGui::Button(("Exp##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    // Export handling is currently routed through the editor/save flow.
                    on_edit_model(model);
                }
                ImGui::SameLine();
                if (ImGui::Button(("Del##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    deleteModel(model);
                }
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
    if (ImGui::BeginPopupModal("Import Model", &show_import_dialog)) {
        ImGui::TextDisabled("(File browser would go here)");
        
        if (ImGui::Button("Cancel")) {
            show_import_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ws::gui::ModelSelector::createModelFromTemplate(const std::string& template_name, const std::string& model_name) {
    // Create new model directory
    std::string model_path = "models/" + model_name + ".simmodel";
    fs::create_directories(model_path);
    
    // Create minimal metadata and version files
    json metadata;
    metadata["name"] = model_name;
    metadata["version"] = "1.0.0";
    metadata["author"] = "User";
    metadata["description"] = "Created from template: " + template_name;
    
    std::ofstream mf(model_path + "/metadata.json");
    mf << metadata.dump(2);
    mf.close();
    
    json version;
    version["format_version"] = "1.0.0";
    version["model_version"] = "1.0.0";
    
    std::ofstream vf(model_path + "/version.json");
    vf << version.dump(2);
    vf.close();
    
    // Create empty model.json
    json model;
    model["id"] = model_name;
    model["version"] = "1.0.0";
    model["grid"] = json::object();
    model["variables"] = json::array();
    model["stages"] = json::array();
    
    std::ofstream mdf(model_path + "/model.json");
    mdf << model.dump(2);
    mdf.close();
    
    refreshModelList();
    if (on_model_created) {
        on_model_created(model_name);
    }
}

void ws::gui::ModelSelector::duplicateModel(const ws::gui::ModelInfo& model) {
    static int counter = 1;
    std::string new_name = model.name + "_copy";
    if (counter > 1) {
        new_name = model.name + "_copy_" + std::to_string(counter);
    }
    counter++;
    
    // Copy model directory
    std::string src_path = model.path;
    std::string dst_path = "models/" + new_name + ".simmodel";
    
    fs::copy(src_path, dst_path, fs::copy_options::recursive);
    
    // Update metadata
    json metadata;
    metadata["name"] = new_name;
    metadata["version"] = "1.0.0";
    
    std::ofstream mf(dst_path + "/metadata.json");
    mf << metadata.dump(2);
    mf.close();
    
    refreshModelList();
}

void ws::gui::ModelSelector::deleteModel(const ws::gui::ModelInfo& model) {
    fs::remove_all(model.path);
    refreshModelList();
}

} // namespace ws::gui
