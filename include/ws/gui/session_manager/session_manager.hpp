#pragma once

#include "ws/core/initialization_binding.hpp"
#include "ws/gui/runtime_service.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ws::gui::session_manager {

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
};

[[nodiscard]] std::string formatBytes(std::uintmax_t bytes);
[[nodiscard]] std::string formatFileTime(const std::filesystem::file_time_type& timePoint, bool available);

} // namespace ws::gui::session_manager
