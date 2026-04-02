#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include <imgui.h>

namespace ws::gui {

struct ModelInfo {
    std::string model_id;
    std::string name;
    std::string path;
    std::string author;
    std::string creation_date;
    std::string description;
    std::string version{"unknown"};
    std::string format_version{"unknown"};
    std::string minimum_engine_version{"unknown"};
    std::string compatibility{"n/a"};
    std::string identity_hash{"unknown"};
    std::vector<std::string> tags;
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
    void open() { window_open = true; }
    void close() { window_open = false; }
    
    // Model operations
    void refreshModelList();
    void createModelFromTemplate(const std::string& template_name,
                                const std::string& model_name);
    void duplicateModel(const ModelInfo& model);
    void renameModel(const ModelInfo& model, const std::string& new_name);
    void exportModel(const ModelInfo& model, const std::filesystem::path& destination);
    void deleteModel(const ModelInfo& model);
    
    // Callbacks
    std::function<void(const ModelInfo&)> on_edit_model;
    std::function<void(const ModelInfo&)> on_load_model;
    // The callback receives the full filesystem path to the created/imported model.
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
    bool show_rename_dialog;
    bool show_export_dialog;
    bool show_delete_confirm_dialog;
    bool show_column_id;
    bool show_column_version;
    bool show_column_format_version;
    bool show_column_minimum_engine_version;
    bool show_column_author;
    bool show_column_creation_date;
    bool show_column_tags;
    bool show_column_description;
    bool show_column_compatibility;
    bool show_column_identity_hash;
    bool show_column_last_modified;
    char pending_rename_name[256];
    char pending_export_path[512];
    char import_source_path[512];
    char import_target_name[256];
    int pending_action_model_index;
    
    // Model data
    std::vector<ModelInfo> models;
    
    // UI helpers
    void renderNewModelDialog();
    void renderImportDialog();
    void renderRenameDialog();
    void renderExportDialog();
    void renderDeleteConfirmDialog();
};

} // namespace ws::gui
