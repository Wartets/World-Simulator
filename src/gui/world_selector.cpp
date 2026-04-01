#include "ws/gui/world_selector.hpp"

namespace ws::gui {

void WorldSelector::refresh(WorldSelectorState& state) {
    std::string message;
    state.worlds = runtimeService_.listStoredWorlds(message);
    state.statusLine = message;
    if (state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.selectedIndex = state.worlds.empty() ? -1 : 0;
    }
    if (state.selectedIndex < 0 && !state.worlds.empty()) {
        state.selectedIndex = 0;
    }
    state.needsRefresh = false;
}

void WorldSelector::duplicateSelected(WorldSelectorState& state, std::string targetName) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = "world_duplicate_failed error=no_selection";
        return;
    }

    const auto& source = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    runtimeService_.duplicateWorld(source, std::move(targetName), message);
    state.statusLine = message;
    state.needsRefresh = true;
}

void WorldSelector::renameSelected(WorldSelectorState& state, std::string targetName) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = "world_rename_failed error=no_selection";
        return;
    }

    const auto& source = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    runtimeService_.renameWorld(source, std::move(targetName), message);
    state.statusLine = message;
    state.needsRefresh = true;
}

void WorldSelector::deleteSelected(WorldSelectorState& state) {
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.worlds.size())) {
        state.statusLine = "world_delete_failed error=no_selection";
        return;
    }

    const auto& selected = state.worlds[static_cast<std::size_t>(state.selectedIndex)].worldName;
    std::string message;
    runtimeService_.deleteWorld(selected, message);
    state.statusLine = message;
    state.needsRefresh = true;
}

} // namespace ws::gui
