#pragma once

#include "ws/gui/runtime_service.hpp"

#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// World Selector State
// =============================================================================

// State for the world selector UI.
struct WorldSelectorState {
    std::vector<StoredWorldInfo> worlds;   // List of available worlds.
    int selectedIndex = -1;                // Currently selected world index.
    bool needsRefresh = true;             // Whether the list needs refresh.
    std::string statusLine;               // Status message to display.
};

// =============================================================================
// World Selector
// =============================================================================

// UI for selecting and managing saved worlds.
class WorldSelector {
public:
    // Constructs a world selector bound to the runtime service.
    explicit WorldSelector(RuntimeService& runtimeService) : runtimeService_(runtimeService) {}

    // Refreshes the list of available worlds.
    void refresh(WorldSelectorState& state);
    // Duplicates the selected world with a new name.
    void duplicateSelected(WorldSelectorState& state, std::string targetName);
    // Renames the selected world.
    void renameSelected(WorldSelectorState& state, std::string targetName);
    // Deletes the selected world.
    void deleteSelected(WorldSelectorState& state);

private:
    RuntimeService& runtimeService_;
};

} // namespace ws::gui
