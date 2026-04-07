#pragma once

#include "ws/core/initialization_binding.hpp"
#include "ws/gui/runtime_service.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui::session_manager {

// Configuration for a single variable's initialization behavior.
struct VariableInitializationSetting {
    std::string variableId;
    bool enabled = false;
    float baseValue = 0.0f;
    int restrictionMode = 0; // 0=None,1=Clamp01,2=NonNegative,3=Clamp[-1,1],4=Tanh,5=Sigmoid
    float clampMin = 0.0f;
    float clampMax = 1.0f;
};

// UI state for the session manager window.
struct SessionUiState {
    std::vector<StoredWorldInfo> worlds;
    int selectedWorldIndex = -1;
    bool needsRefresh = true;
    int pendingDeleteWorldIndex = -1;
    char pendingWorldName[128] = "";
    char selectedModelName[128] = "";
    char selectedModelDescription[512] = "";
    char selectedModelAuthor[128] = "";
    char selectedModelVersion[64] = "";
    char selectedModelPath[512] = "";
    initialization::ModelVariableCatalog selectedModelCatalog{};
    initialization::InitializationBindingPlan generationBindingPlan{};
    std::vector<std::string> selectedModelCellStateVariables;
    bool allowUnresolvedGenerationBindings = false;
    bool allowHeavyInitializationWork = false;
    bool generationShowOnlyViableModes = false;
    int wizardStepIndex = 0;
    int generationPreviewSourceIndex = 0;
    int generationPreviewChannelIndex = 0;
    int generationModeIndex = -1;
    std::vector<VariableInitializationSetting> variableInitializationSettings;
    char worldSearch[128] = "";
    bool filterCheckpointOnly = false;
    bool filterProfileOnly = false;
    int sortMode = 1;

    int pendingDuplicateWorldIndex = -1;
    int pendingRenameWorldIndex = -1;
    int pendingExportWorldIndex = -1;
    char pendingDuplicateName[128] = "";
    char pendingRenameName[128] = "";
    char pendingExportPath[260] = "";
    char pendingImportPath[260] = "";

    char statusMessage[256] = "";
    char operationLabel[128] = "";
    char operationDetail[256] = "";
    float operationProgress = -1.0f;
    bool operationActive = false;
    float lastOperationDurationMs = 0.0f;
};

// Formats byte count as human-readable string (KB, MB, GB).
[[nodiscard]] std::string formatBytes(std::uintmax_t bytes);
// Formats file timestamp for display in UI.
[[nodiscard]] std::string formatFileTime(const std::filesystem::file_time_type& timePoint, bool available);

} // namespace ws::gui::session_manager
