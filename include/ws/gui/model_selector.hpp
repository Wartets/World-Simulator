#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include <imgui.h>

namespace ws::gui {

struct ModelInfo {
    std::string name;
    std::string path;
    std::string version{"unknown"};
    std::filesystem::file_time_type last_modified;
};

class ModelSelector {
public:
    ModelSelector();
    ~ModelSelector();
    
    // Rendering
    void render(ImVec2 available_size);
    
    // State
    bool isOpen() const { return window_open; }
    void close() { window_open = false; }
    
    // Model operations
    void refreshModelList();
    void createModelFromTemplate(const std::string& template_name,
                                const std::string& model_name);
    void duplicateModel(const ModelInfo& model);
    void deleteModel(const ModelInfo& model);
    
    // Callbacks
    std::function<void(const ModelInfo&)> on_edit_model;
    std::function<void(const std::string&)> on_model_created;
    
    // Accessors
    const std::vector<ModelInfo>& getModels() const { return models; }
    int getSelectedModelIndex() const { return selected_model_index; }
    
private:
    // Window state
    bool window_open;
    int selected_model_index;
    bool show_new_model_dialog;
    bool show_import_dialog;
    
    // Model data
    std::vector<ModelInfo> models;
    
    // UI helpers
    void renderNewModelDialog();
    void renderImportDialog();
};

} // namespace ws::gui
