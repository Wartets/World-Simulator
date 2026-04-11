#include "ws/gui/world_selector.hpp"

#include "ws/gui/status_message.hpp"

namespace ws::gui {

// Refreshes world list from runtime service and updates selector state.
// Queries stored worlds, validates selection index, sets needsRefresh flag.
// @param state Selector state to update with refreshed world list
void WorldSelector::refresh(WorldSelectorState& state) {
    std::string message;
    state.worlds = runtimeService_.listStoredWorlds(message);
    state.statusLine = formatOperationMessageForDisplay(translateOperationMessage(message));
    if (state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.selectedIndex = state.worlds.empty() ? -1 : 0;
    }
    if (state.selectedIndex < 0 && !state.worlds.empty()) {
        state.selectedIndex = 0;
    }
    state.needsRefresh = false;
}

// Duplicates selected world with new name.
// @param state Current selector state with selected index
// @param targetName Name for the duplicated world
void WorldSelector::duplicateSelected(WorldSelectorState& state, std::string targetName) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = formatOperationMessageForDisplay(translateOperationMessage("world_duplicate_failed error=no_selection"));
        return;
    }

    const auto& source = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    [[maybe_unused]] const bool duplicated = runtimeService_.duplicateWorld(source, std::move(targetName), message);
    state.statusLine = formatOperationMessageForDisplay(translateOperationMessage(message));
    state.needsRefresh = true;
}

// Renames selected world to new name.
// @param state Current selector state with selected index
// @param targetName New name for the world
void WorldSelector::renameSelected(WorldSelectorState& state, std::string targetName) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = formatOperationMessageForDisplay(translateOperationMessage("world_rename_failed error=no_selection"));
        return;
    }

    const auto& source = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    [[maybe_unused]] const bool renamed = runtimeService_.renameWorld(source, std::move(targetName), message);
    state.statusLine = formatOperationMessageForDisplay(translateOperationMessage(message));
    state.needsRefresh = true;
}

// Deletes selected world from storage.
// @param state Current selector state with selected index
void WorldSelector::deleteSelected(WorldSelectorState& state) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = formatOperationMessageForDisplay(translateOperationMessage("world_delete_failed error=no_selection"));
        return;
    }

    const auto& selected = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    [[maybe_unused]] const bool deleted = runtimeService_.deleteWorld(selected, message);
    state.statusLine = formatOperationMessageForDisplay(translateOperationMessage(message));
    state.needsRefresh = true;
}

} // namespace ws::gui
