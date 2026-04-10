#pragma once

#include "ws/gui/main_window/window_state_store.hpp"

#include <vector>

namespace ws::gui::main_window {

enum class AppState { ModelSelector, ModelEditor, SessionManager, NewWorldWizard, Simulation };
enum class OverlayIcon { None, Play, Pause };

struct OverlayState {
    float alpha = 0.0f;
    OverlayIcon icon = OverlayIcon::None;
};

struct WorkflowState {
    AppState current = AppState::ModelSelector;
    std::vector<AppState> history{};
    int historyCursor = 0;
    bool historyTraversalInProgress = false;

    bool taskRailAnalyzeSelected = false;
    bool showShortcutHelpModal = false;
    bool showStopResetConfirm = false;
    bool showCheckpointDeleteConfirm = false;
    bool showWizardResetConfirm = false;
    bool workflowRailAdvancedMode = false;
};

struct MainWindowAppState {
    PersistedWindowState persistedWindowState{};
    WorkflowState workflow{};
    OverlayState overlay{};
};

} // namespace ws::gui::main_window
