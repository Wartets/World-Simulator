#pragma once

#include "ws/gui/runtime_service.hpp"

#include <string>
#include <vector>

namespace ws::gui {

struct WorldSelectorState {
    std::vector<StoredWorldInfo> worlds;
    int selectedIndex = -1;
    bool needsRefresh = true;
    std::string statusLine;
};

class WorldSelector {
public:
    explicit WorldSelector(RuntimeService& runtimeService) : runtimeService_(runtimeService) {}

    void refresh(WorldSelectorState& state);
    void duplicateSelected(WorldSelectorState& state, std::string targetName);
    void renameSelected(WorldSelectorState& state, std::string targetName);
    void deleteSelected(WorldSelectorState& state);

private:
    RuntimeService& runtimeService_;
};

} // namespace ws::gui
