#include "ws/gui/model_selector.hpp"

#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ws::gui {

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
            
            // Try to load version info
            std::string version_path = (entry.path() / "version.json").string();
            if (fs::exists(version_path)) {
                try {
                    std::ifstream vf(version_path);
                    json vj = json::parse(vf);
                    if (vj.contains("model_version")) {
                        info.version = vj["model_version"].get<std::string>();
                    }
                } catch (...) {
                    info.version = "unknown";
                }
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
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", ImVec2(100, 0))) {
            show_import_dialog = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100, 0))) {
            refreshModelList();
        }
        
        ImGui::Separator();
        
        // Model table
        if (ImGui::BeginTable("ModelsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Version");
            ImGui::TableSetupColumn("Last Modified");
            ImGui::TableSetupColumn("Grid");
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
                ImGui::TextDisabled("%.0f", static_cast<float>(model.last_modified.time_since_epoch().count()));
                
                // Grid column
                ImGui::TableNextColumn();
                ImGui::TextDisabled("(dynamic)");
                
                // Actions column
                ImGui::TableNextColumn();
                if (ImGui::Button(("Edit##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    on_edit_model(model);
                }
                ImGui::SameLine();
                if (ImGui::Button(("Dup##" + std::to_string(i)).c_str(), ImVec2(40, 0))) {
                    duplicateModel(model);
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
        }
        if (ImGui::Selectable("Advection-Diffusion", false)) {
            createModelFromTemplate("advection_diffusion", model_name_buffer);
            show_new_model_dialog = false;
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Cancel")) {
            show_new_model_dialog = false;
        }
        
        ImGui::EndPopup();
    }
}

void ModelSelector::renderImportDialog() {
    if (ImGui::BeginPopupModal("Import Model", &show_import_dialog)) {
        ImGui::TextDisabled("(File browser would go here)");
        
        if (ImGui::Button("Cancel")) {
            show_import_dialog = false;
        }
        
        ImGui::EndPopup();
    }
}

void ModelSelector::createModelFromTemplate(const std::string& template_name,
                                           const std::string& model_name) {
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
}

void ModelSelector::duplicateModel(const ModelInfo& model) {
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

void ModelSelector::deleteModel(const ModelInfo& model) {
    fs::remove_all(model.path);
    refreshModelList();
}

} // namespace ws::gui
