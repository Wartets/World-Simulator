#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

#include "ws/gui/main_window/platform_dialogs.hpp"

// Records current app state to history stack.
// Trims forward history when new state is recorded during navigation.
void recordAppStateHistory() {
    if (appStateHistory_.empty()) {
        appStateHistory_.push_back(appState_);
        appStateHistoryCursor_ = 0;
        return;
    }

    if (appStateHistoryTraversalInProgress_) {
        appStateHistoryTraversalInProgress_ = false;
        return;
    }

    const int clampedCursor = std::clamp(appStateHistoryCursor_, 0, static_cast<int>(appStateHistory_.size()) - 1);
    appStateHistoryCursor_ = clampedCursor;
    if (appStateHistory_[static_cast<std::size_t>(appStateHistoryCursor_)] == appState_) {
        return;
    }

    if (appStateHistoryCursor_ + 1 < static_cast<int>(appStateHistory_.size())) {
        appStateHistory_.erase(
            appStateHistory_.begin() + static_cast<std::ptrdiff_t>(appStateHistoryCursor_ + 1),
            appStateHistory_.end());
    }
    appStateHistory_.push_back(appState_);
    appStateHistoryCursor_ = static_cast<int>(appStateHistory_.size()) - 1;
}

// Navigates app state history in given direction (-1 back, +1 forward).
// @param direction Navigation direction: negative for back, positive for forward
void navigateAppStateHistory(const int direction) {
    if (appStateHistory_.empty()) {
        return;
    }

    const int minIndex = 0;
    const int maxIndex = static_cast<int>(appStateHistory_.size()) - 1;
    const int nextIndex = std::clamp(appStateHistoryCursor_ + direction, minIndex, maxIndex);
    if (nextIndex == appStateHistoryCursor_) {
        return;
    }

    appStateHistoryCursor_ = nextIndex;
    appState_ = appStateHistory_[static_cast<std::size_t>(appStateHistoryCursor_)];
    appStateHistoryTraversalInProgress_ = true;
}

// Handles global keyboard shortcuts (F1 for help, etc.).
// Processes key presses that work regardless of current panel focus.
[[nodiscard]] const std::array<std::pair<const char*, const char*>, 10>& shortcutReferenceRows() const {
    static constexpr std::array<std::pair<const char*, const char*>, 10> kRows = {{
        {"F1", "Open keyboard shortcut reference"},
        {"Alt+Left", "Navigate to previous application state"},
        {"Alt+Right", "Navigate to next application state"},
        {"Space", "Toggle play / pause"},
        {"Ctrl+S", "Save active world"},
        {"Right Arrow", "Step forward (when paused)"},
        {"R", "Reset active viewport camera"},
        {"+ / -", "Zoom active viewport"},
        {"F2 - F12", "Select viewport editor tab (Simulation state)"},
        {"Escape", "Clear focused ImGui widget"},
    }};
    return kRows;
}

void drawShortcutReferenceTable(const char* tableId) const {
    const auto& rows = shortcutReferenceRows();
    ImGui::Columns(2, tableId, false);
    ImGui::TextDisabled("Shortcut"); ImGui::NextColumn();
    ImGui::TextDisabled("Action"); ImGui::NextColumn();
    ImGui::Separator();
    for (const auto& row : rows) {
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.5f, 1.0f), "%s", row.first); ImGui::NextColumn();
        ImGui::TextUnformatted(row.second); ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void handleGlobalKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        showShortcutHelpModal_ = true;
    }

    if (io.WantCaptureKeyboard) {
        return;
    }

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        navigateAppStateHistory(-1);
    }
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        navigateAppStateHistory(1);
    }
}

// Draws shortcut help modal dialog.
void drawShortcutHelpModal() {
    if (showShortcutHelpModal_) {
        ImGui::OpenPopup("Keyboard Shortcuts");
        showShortcutHelpModal_ = false;
    }

    bool popupOpen = true;
    if (!ImGui::BeginPopupModal("Keyboard Shortcuts", &popupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("State history");
    ImGui::Text("Current state: %s", appStateLabel(static_cast<int>(appState_)));
    ImGui::Text("History position: %d / %d",
        appStateHistory_.empty() ? 0 : (appStateHistoryCursor_ + 1),
        static_cast<int>(appStateHistory_.size()));
    ImGui::Separator();

    drawShortcutReferenceTable("shortcuthelpcols");
    ImGui::Spacing();

    if (PrimaryButton("Close", ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    if (!popupOpen) {
        ImGui::CloseCurrentPopup();
    }
}

// Keyboard shortcut handling - called once per frame before ImGui::NewFrame
// Handles keyboard shortcuts for simulation control.
// Called once per frame before ImGui::NewFrame.
void handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    // Space -> toggle pause / resume
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (runtime_.isRunning()) {
            if (runtime_.isPaused()) {
                std::string msg;
                if (runtime_.resume(msg)) {
                    viz_.autoRun = true;
                    appendUserFacingOperationMessage(msg, "Simulation resumed", "Use Pause or Step controls to manage runtime progression.");
                    triggerOverlay(OverlayIcon::Play);
                }
            } else {
                cancelPendingSimulationSteps();
                std::string msg;
                if (runtime_.pause(msg)) {
                    viz_.autoRun = false;
                    appendUserFacingOperationMessage(msg, "Simulation paused", "Apply interventions, replay, or step manually before resuming.");
                    triggerOverlay(OverlayIcon::Pause);
                }
            }
        }
    }

    // Ctrl+S -> save active world
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (!runtime_.activeWorldName().empty()) {
            saveDisplayPrefs();
            std::string msg;
            [[maybe_unused]] const bool saved = runtime_.saveActiveWorld(msg);
            appendUserFacingOperationMessage(msg, "Active world save requested", "If save fails, verify storage paths and retry.");
        }
    }

    // Right arrow -> single step (when paused)
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && runtime_.isRunning() && runtime_.isPaused()) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), msg);
        appendUserFacingOperationMessage(msg, "Manual step requested", "Inspect updated state or continue stepping.");
        requestSnapshotRefresh();
    }

    ensureViewportStateConsistency();

    // F2-F12 -> switch active viewport editor (reserve F1 for global help)
    static constexpr std::array<ImGuiKey, 11> kViewportHotkeys = {
        ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5,
        ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9,
        ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12};
    for (std::size_t i = 0; i < kViewportHotkeys.size(); ++i) {
        if (ImGui::IsKeyPressed(kViewportHotkeys[i], false) && i < viz_.viewports.size()) {
            requestViewportEditorSelection(i);
        }
    }

    // Escape -> return focus to viewport (clear ImGui input focus)
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::SetWindowFocus(nullptr);

    // +/- -> adjust active viewport zoom
    const int maxViewportIndex = static_cast<int>(viz_.viewports.size()) - 1;
    const std::size_t activeIndex = static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, std::max(0, maxViewportIndex)));
    if (ImGui::IsKeyDown(ImGuiKey_Equal)) {
        const auto& cam = viewportManager_.camera(activeIndex);
        viewportManager_.setZoom(activeIndex, cam.zoom * 1.02f);
    }
    if (ImGui::IsKeyDown(ImGuiKey_Minus)) {
        const auto& cam = viewportManager_.camera(activeIndex);
        viewportManager_.setZoom(activeIndex, cam.zoom * 0.98f);
    }

    // R -> reset active viewport zoom/pan
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        viewportManager_.fit(activeIndex);
    }
}

// Returns to model selector, optionally saving active world.
// @param saveActiveWorld Whether to save current world state
void returnToModelSelector(const bool saveActiveWorld) {
    viz_.autoRun = false;
    cancelPendingSimulationSteps();

    const bool running = runtime_.isRunning();
    if (saveActiveWorld && running && !runtime_.activeWorldName().empty()) {
        saveDisplayPrefs();
        std::string saveMsg;
        [[maybe_unused]] const bool saved = runtime_.saveActiveWorld(saveMsg);
        appendUserFacingOperationMessage(saveMsg, "Save active world before returning", "Return to model selector after save confirmation.");
    }

    std::string stopMsg;
    runtime_.stop(stopMsg);
    if (!stopMsg.empty()) {
        appendUserFacingOperationMessage(stopMsg, "Runtime stop requested", "Choose a model or world to continue.");
    }

    appState_ = AppState::ModelSelector;
    modelSelector_.open();
}

bool seekToStepAndRefresh(const std::uint64_t targetStep) {
    cancelPendingSimulationSteps();
    std::string msg;
    const bool ok = runtime_.seekStep(targetStep, msg);
    appendUserFacingOperationMessage(
        msg,
        "Seek to target step requested",
        ok ? "Inspect synced state at target step." : "Verify target step and runtime state, then retry seek.");
    if (ok) {
        panel_.seekTargetStep = targetStep;
        requestSnapshotRefresh();
    }
    return ok;
}

struct SimulationClockInfo {
    float value = 0.0f;
    std::string sourceLabel = "runtime ticks";
    bool fromField = false;
};

[[nodiscard]] SimulationClockInfo currentSimulationClock() const {
    SimulationClockInfo info;
    if (!viz_.hasCachedCheckpoint) {
        return info;
    }

    info.value = static_cast<float>(viz_.cachedCheckpoint.stateSnapshot.header.timestampTicks);

    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    const auto isUsableScalar = [](const auto& field) {
        return field.values.size() == 1u && std::isfinite(field.values.front());
    };

    const auto adoptField = [&info](const auto& field) {
        info.value = field.values.front();
        info.sourceLabel = "field " + field.spec.name;
        info.fromField = true;
    };

    for (const auto& field : fields) {
        if (!isUsableScalar(field)) {
            continue;
        }
        if (field.spec.name == "time" || field.spec.name == "simulation_time" || field.spec.name == "t") {
            adoptField(field);
            return info;
        }
    }

    for (const auto& field : fields) {
        if (!isUsableScalar(field)) {
            continue;
        }
        if (containsToken(field.spec.name, {"time_of_day", "clock_time", "elapsed_time", "sim_time"})) {
            adoptField(field);
            return info;
        }
    }

    return info;
}

[[nodiscard]] static std::string formatUserFacingOperationMessage(
    const std::string& rawMessage,
    const std::string& fallbackWhat,
    const std::string& fallbackNext) {
    const OperationResult result = translateOperationResult(rawMessage);
    const bool failed = result.status == OperationStatus::Failure;
    const bool warned = result.status == OperationStatus::Warning;

    std::string what = fallbackWhat.empty() ? result.message : fallbackWhat;
    if (what.empty()) {
        what = "Operation update";
    }

    std::string why = "operation completed";
    if (failed) {
        why = "runtime reported a failure state";
    } else if (warned) {
        why = "runtime reported a warning state";
    }

    std::string next = fallbackNext;
    if (next.empty()) {
        if (failed) {
            next = "Review controls and retry after resolving the reported reason.";
        } else if (warned) {
            next = "Review the warning details before continuing.";
        } else {
            next = "Continue with the next workflow step.";
        }
    }

    std::ostringstream out;
    out << "What happened: " << what
        << " | Why: " << why
        << " | Next: " << next;
    if (!result.technicalDetail.empty()) {
        out << " | Technical: " << result.technicalDetail;
    } else if (!rawMessage.empty()) {
        out << " | Technical: " << rawMessage;
    }
    return out.str();
}

void appendUserFacingOperationMessage(
    const std::string& rawMessage,
    const std::string& fallbackWhat,
    const std::string& fallbackNext) {
    appendLog(formatUserFacingOperationMessage(rawMessage, fallbackWhat, fallbackNext));
}

// Draws main menu bar with application menus.
void drawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    const bool running = runtime_.isRunning();
    const bool paused = runtime_.isPaused();
    const bool hasWorld = !runtime_.activeWorldName().empty();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Active World", "Ctrl+S", false, running && hasWorld)) {
            saveDisplayPrefs();
            std::string msg;
            [[maybe_unused]] const bool saved = runtime_.saveActiveWorld(msg);
            appendUserFacingOperationMessage(msg, "Active world save requested", "Continue simulation or return to models.");
        }
        if (ImGui::MenuItem(hasWorld ? "Save & Return to Models" : "Return to Models")) {
            returnToModelSelector(hasWorld);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo Last Manual Edit", nullptr, false, running)) {
            std::string undoMsg;
            const bool undone = runtime_.undoLastManualPatch(undoMsg);
            appendUserFacingOperationMessage(
                undoMsg,
                "Undo last manual edit requested",
                undone ? "Review reverted state and continue editing." : "No reversible edit found; apply a new edit if needed.");
            if (undone) {
                requestSnapshotRefresh();
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Field History", "H", showFieldHistoryWindow_)) {
            showFieldHistoryWindow_ = !showFieldHistoryWindow_;
        }
        if (ImGui::MenuItem("Save Display Preferences", nullptr, false, hasWorld)) {
            saveDisplayPrefs();
            appendUserFacingOperationMessage(
                "display_prefs_saved world=" + runtime_.activeWorldName(),
                "Display preferences saved",
                "Continue with current layout or adjust viewport configuration.");
        }
        if (ImGui::MenuItem("Reload Display Preferences", nullptr, false, hasWorld)) {
            resetDisplayConfigToDefaults();
            loadDisplayPrefs();
            appendUserFacingOperationMessage(
                "display_prefs_reloaded world=" + runtime_.activeWorldName(),
                "Display preferences reloaded",
                "Confirm viewport layout and continue simulation analysis.");
        }
        if (ImGui::MenuItem("Add Viewport", nullptr, false, viz_.viewports.size() < kMaxDynamicViewportCount)) {
            addViewportConfig();
        }
        if (ImGui::MenuItem("Close Active Viewport", nullptr, false, viz_.viewports.size() > 1u)) {
            removeViewportConfig(static_cast<std::size_t>(std::clamp(
                viz_.activeViewportEditor,
                0,
                static_cast<int>(viz_.viewports.size()) - 1)));
        }
        if (ImGui::MenuItem("Reset Active Camera", "R", false, !viz_.viewports.empty())) {
            const std::size_t activeIndex = static_cast<std::size_t>(std::clamp(
                viz_.activeViewportEditor,
                0,
                std::max(0, static_cast<int>(viz_.viewports.size()) - 1)));
            viewportManager_.fit(activeIndex);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Simulation")) {
        if (ImGui::MenuItem((paused || !viz_.autoRun) ? "Play" : "Pause", "Space", false, running)) {
            if (paused || !viz_.autoRun) {
                std::string msg;
                runtime_.resume(msg);
                viz_.autoRun = true;
                appendUserFacingOperationMessage(msg, "Simulation resumed", "Pause to intervene or keep running for continuous updates.");
                requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Play);
            } else {
                cancelPendingSimulationSteps();
                std::string msg;
                runtime_.pause(msg);
                viz_.autoRun = false;
                appendUserFacingOperationMessage(msg, "Simulation paused", "Apply edits/replay or step manually before resuming.");
                triggerOverlay(OverlayIcon::Pause);
            }
        }
        if (ImGui::MenuItem("Step Forward", "Right", false, running && paused)) {
            cancelPendingSimulationSteps();
            std::string msg;
            runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), msg);
            appendUserFacingOperationMessage(msg, "Manual step requested", "Inspect new state, then continue stepping or resume.");
            requestSnapshotRefresh();
        }
        if (ImGui::MenuItem("Seek To Target Step", nullptr, false, running)) {
            seekToStepAndRefresh(panel_.seekTargetStep);
        }
        if (ImGui::MenuItem("Store Quick Checkpoint", nullptr, false, running)) {
            std::string msg;
            [[maybe_unused]] const bool created = runtime_.createCheckpoint(panel_.checkpointLabel[0] != '\0' ? panel_.checkpointLabel : "quick", msg);
            appendUserFacingOperationMessage(msg, "Quick checkpoint requested", "Use checkpoint restore to compare or roll back state.");
        }
        ImGui::EndMenu();
    }

    if (running && viz_.hasCachedCheckpoint) {
        ImGui::Separator();
        ImGui::TextDisabled(
            "Step %llu",
            static_cast<unsigned long long>(viz_.cachedCheckpoint.stateSnapshot.header.stepIndex));
        if (hasWorld) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", runtime_.activeWorldName().c_str());
        }
    }

    ImGui::EndMainMenuBar();
}

// Main control panel window
// Draws main control panel window.
void drawControlPanel() {
    handleKeyboardShortcuts();

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500.0f, 780.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 400.0f), ImVec2(700.0f, 1400.0f));

    ImGui::Begin("Control Panel##main", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    drawStatusHeader();

    if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_Reorderable)) {
        if (ImGui::BeginTabItem("Simulation"))  { drawSimulationTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Interventions")) { drawInterventionsTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Display"))     { drawDisplayTab();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Analysis"))    { drawAnalysisTab();    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Diagnostics")) { drawDiagnosticsTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("System"))      { drawSystemTab();      ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    drawConfirmationModals();

    ImGui::End();
}

// Status header - always visible, always up-to-date
// Draws status header with runtime information.
void drawStatusHeader() {
    const bool running  = runtime_.isRunning();
    const bool paused   = runtime_.isPaused();
    const bool hasWorld = !runtime_.activeWorldName().empty();

    // top action bar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 16, 28, 255));
    ImGui::BeginChild("StatusHdr", ImVec2(0.0f, 188.0f), true);

    // Save & Return button
    {
        const char* label = hasWorld ? "Save & Exit  [Ctrl+S]" : "Back to Models";
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(40, 80, 140, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 105, 175, 240));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(30, 65, 115, 255));
        if (ImGui::Button(label, ImVec2(160.0f, 30.0f))) {
            returnToModelSelector(hasWorld);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Saves the active world (profile + checkpoint) and returns to the model selector.\nShortcut: Ctrl+S (save only)");
        }
    }

    ImGui::SameLine(0.0f, 10.0f);

    // Status badges
    if (running) {
        StatusBadge("RUNNING", true);
        ImGui::SameLine(0.0f, 6.0f);
        StatusBadge(paused ? "PAUSED" : "LIVE", !paused);
    } else {
        StatusBadge("STOPPED", false);
    }

    ImGui::SameLine(0.0f, 10.0f);

    if (SecondaryButton("Back##state", ImVec2(68.0f, 24.0f))) {
        navigateAppStateHistory(-1);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Navigate to previous state. Shortcut: Alt+Left");
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (SecondaryButton("Next##state", ImVec2(68.0f, 24.0f))) {
        navigateAppStateHistory(1);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Navigate to next state. Shortcut: Alt+Right");
    }

    // Quick play/pause toggle
    if (running) {
        const char* playLabel = (paused || !viz_.autoRun) ? "Play" : "Pause";
        ImGui::PushStyleColor(ImGuiCol_Button,
            (paused || !viz_.autoRun) ? IM_COL32(30, 110, 60, 220) : IM_COL32(100, 80, 20, 200));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            (paused || !viz_.autoRun) ? IM_COL32(45, 140, 80, 240) : IM_COL32(130, 105, 30, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(20, 90, 45, 255));
        if (ImGui::Button(playLabel, ImVec2(84.0f, 30.0f))) {
            if (paused || !viz_.autoRun) {
                std::string msg; runtime_.resume(msg); viz_.autoRun = true;
                appendUserFacingOperationMessage(msg, "Simulation resumed", "Pause when ready to apply deterministic edits or replay.");
                requestSnapshotRefresh(); triggerOverlay(OverlayIcon::Play);
            } else {
                cancelPendingSimulationSteps();
                std::string msg; runtime_.pause(msg); viz_.autoRun = false;
                appendUserFacingOperationMessage(msg, "Simulation paused", "Use step, replay, or manual edit controls before resuming.");
                triggerOverlay(OverlayIcon::Pause);
            }
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Toggle play / pause.  Shortcut: Space");
    }

    // info row
    ImGui::Spacing();
    if (viz_.hasCachedCheckpoint) {
        const auto& snap = viz_.cachedCheckpoint.stateSnapshot;
        const auto& sig  = viz_.cachedCheckpoint.runSignature;
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f),
            "Step %-9llu  %ux%u  Profile %s  %s",
            (unsigned long long)snap.header.stepIndex,
            sig.grid().width, sig.grid().height,
            toString(runtime_.config().tier).c_str(),
            app::temporalPolicyToString(sig.temporalPolicy()).c_str());
        if (hasWorld) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.65f, 1.0f), " - %s",
                runtime_.activeWorldName().c_str());
        }
    } else {
        ImGui::TextDisabled("No simulation active.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("State: %s", appStateLabel(static_cast<int>(appState_)));

    bool pendingRestart = false;
    int runtimeTierIndex = 0;
    int runtimeTemporalIndex = 0;
    if (running) {
        const auto& cfg = runtime_.config();
        runtimeTierIndex =
            (cfg.tier == ModelTier::A) ? 0 :
            (cfg.tier == ModelTier::B) ? 1 : 2;
        const std::string runtimeTemporal = app::temporalPolicyToString(cfg.temporalPolicy);
        runtimeTemporalIndex =
            (runtimeTemporal == "uniform") ? 0 :
            (runtimeTemporal == "phased") ? 1 : 2;

        pendingRestart =
            runtimeTierIndex != panel_.tierIndex ||
            runtimeTemporalIndex != panel_.temporalIndex;
    }

    std::size_t pendingImmediateWrites = 0;
    std::size_t queuedDeferredEvents = 0;
    std::size_t pendingScheduledPerturbations = 0;
    std::size_t runtimeManualEventCount = 0;
    std::string effectLedgerMessage;
    const bool hasLedgerCounts = running && runtime_.effectLedgerCounts(
        pendingImmediateWrites,
        queuedDeferredEvents,
        pendingScheduledPerturbations,
        runtimeManualEventCount,
        effectLedgerMessage);

    ImGui::TextDisabled("Global effect ledger");
    int ledgerEntryCount = 0;

    if (pendingImmediateWrites > 0) {
        ++ledgerEntryCount;
        ImGui::TextColored(
            ImVec4(0.50f, 0.85f, 0.55f, 1.0f),
            "- Pending immediate writes: %zu patch%s",
            pendingImmediateWrites,
            pendingImmediateWrites == 1u ? "" : "es");
        ImGui::Indent();
        ImGui::TextDisabled("Source: Interventions > Parameter Control & Live Patching");
        ImGui::TextDisabled("Commit/Revert: Step or Play to ingest queued writes; revert with Undo Last Manual Edit or by applying a replacement value.");
        ImGui::Unindent();
    }

    if (queuedDeferredEvents > 0 || pendingScheduledPerturbations > 0) {
        ++ledgerEntryCount;
        ImGui::TextColored(
            ImVec4(0.98f, 0.78f, 0.45f, 1.0f),
            "- Queued deferred events: %zu queue item%s, %zu scheduled perturbation%s",
            queuedDeferredEvents,
            queuedDeferredEvents == 1u ? "" : "s",
            pendingScheduledPerturbations,
            pendingScheduledPerturbations == 1u ? "" : "s");
        ImGui::Indent();
        ImGui::TextDisabled("Source: Interventions > Perturbation & Forcing Tools, Edit > Undo Last Manual Edit");
        ImGui::TextDisabled("Commit/Revert: Continue stepping to apply deferred events; pause and use Undo Last Manual Edit to cancel the latest reversible cell edit.");
        ImGui::Unindent();
    }

    if (pendingRestart) {
        ++ledgerEntryCount;
        ImGui::TextColored(
            ImVec4(0.95f, 0.80f, 0.45f, 1.0f),
            "- Restart-required changes: execution profile or temporal behavior differs from runtime");
        ImGui::Indent();
        ImGui::TextDisabled("Source: Simulation > Execution Profile & Temporal Behavior");
        ImGui::TextDisabled("Commit/Revert: Apply settings (runtime restart) to commit, or restore profile/temporal controls to the active runtime values.");
        if (SecondaryButton("Revert profile/temporal to runtime", ImVec2(260.0f, 22.0f))) {
            panel_.tierIndex = runtimeTierIndex;
            panel_.temporalIndex = runtimeTemporalIndex;
        }
        ImGui::Unindent();
    }

    if (runtimeManualEventCount > 0) {
        ++ledgerEntryCount;
        ImGui::TextColored(
            ImVec4(0.62f, 0.82f, 0.95f, 1.0f),
            "- Unsaved runtime changes: %zu manual event%s",
            runtimeManualEventCount,
            runtimeManualEventCount == 1u ? "" : "s");
        ImGui::Indent();
        ImGui::TextDisabled("Source: Interventions panels and runtime control edits");
        ImGui::TextDisabled("Commit/Revert: Save Active World (Ctrl+S) to persist; use Undo Last Manual Edit or checkpoint restore to revert runtime state.");
        ImGui::Unindent();
    }

    if (!running) {
        ImGui::TextDisabled("No active runtime. Ledger entries appear after simulation starts.");
    } else if (!hasLedgerCounts) {
        ImGui::TextDisabled("Effect ledger unavailable: %s", effectLedgerMessage.c_str());
    } else if (ledgerEntryCount == 0) {
        ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "No pending runtime effects.");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

//
// Tab: Simulation
//
// Draws simulation tab with controls.
void drawSimulationTab() {
    ImGui::BeginChild("SimTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Execution profile / temporal behavior
    PushSectionTint(0);
    if (ImGui::CollapsingHeader("Execution Profile & Temporal Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawTierSelector();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Playback
    PushSectionTint(1);
    if (ImGui::CollapsingHeader("Engine Lifecycle & Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawPlaybackSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Stepping
    PushSectionTint(2);
    if (ImGui::CollapsingHeader("Step Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawStepControlsSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Phase 8: Time control & scrubbing
    PushSectionTint(3);
    if (ImGui::CollapsingHeader("Time Control & Scrubbing", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawTimeControlSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Checkpoints
    PushSectionTint(3);
    if (ImGui::CollapsingHeader("In-Memory Checkpoints")) {
        drawCheckpointSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Guardrails
    PushSectionTint(4);
    if (ImGui::CollapsingHeader("Numeric Guardrails")) {
        drawGuardrailsSection();
    }
    PopSectionTint();

    ImGui::EndChild();
}

// Draws interventions tab with perturbation controls.
void drawInterventionsTab() {
    ImGui::BeginChild("InterventionsTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    PushSectionTint(8);
    if (ImGui::CollapsingHeader("Runtime interaction mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int interactionModeIndex = 0;
        static constexpr const char* kInteractionModes[] = {
            "Inspect", "Paint / Patch", "Experiment Replay"
        };
        ImGui::Combo("Mode", &interactionModeIndex, kInteractionModes, static_cast<int>(std::size(kInteractionModes)));

        if (interactionModeIndex == 0) {
            ImGui::TextDisabled("Inspect mode: use runtime view hover readouts and Analysis tab summaries.");
        } else if (interactionModeIndex == 1) {
            ImGui::TextDisabled("Paint / Patch mode: use Manual cell/global edit controls below while paused.");
        } else {
            ImGui::TextDisabled("Experiment Replay mode: load an event log, run preflight, capture baseline, then replay compatible entries.");
        }
    }
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(9);
    if (ImGui::CollapsingHeader("Parameter Control & Live Patching", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawParameterControlSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(10);
    if (ImGui::CollapsingHeader("Perturbation & Forcing Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawPerturbationSection();
    }
    PopSectionTint();

    ImGui::EndChild();
}

// Draws parameter control section.
void drawParameterControlSection() {
    static bool liveApplyWhileRunning = true;

    if (!runtime_.isRunning()) {
        panel_.parameterValueDirty = false;
        panel_.selectedParameterName[0] = '\0';
        ImGui::TextDisabled("Start the simulation to edit runtime parameters.");
        return;
    }

    ImGui::TextDisabled("Change scope");
    ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Applies now");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Paused only");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Saved with world (after explicit save)");
    ImGui::TextDisabled("Parameter sliders can apply live. Manual cell/global edits require pause. Save Active World to persist runtime edits.");

    checkboxWithHint("Live apply while running", &liveApplyWhileRunning,
        "When enabled, parameter slider edits are applied immediately, including while simulation is running.");
    if (!runtime_.isPaused()) {
        ImGui::TextDisabled("Runtime is active: parameter sliders can apply live; manual cell edits still require pause.");
    }

    std::vector<ParameterControl> controls;
    std::string msg;
    runtime_.parameterControls(controls, msg);

    if (!controls.empty()) {
        panel_.selectedParameterIndex = std::clamp(panel_.selectedParameterIndex, 0, static_cast<int>(controls.size() - 1));
        const auto& selected = controls[static_cast<std::size_t>(panel_.selectedParameterIndex)];

        const bool selectionChanged = (std::string(panel_.selectedParameterName) != selected.name);
        if (selectionChanged || !panel_.parameterValueDirty) {
            panel_.parameterValue = selected.value;
            std::snprintf(panel_.selectedParameterName, sizeof(panel_.selectedParameterName), "%s", selected.name.c_str());
            panel_.parameterValueDirty = false;
        }

        if (ImGui::BeginCombo("Writable parameter##phase6", selected.name.c_str())) {
            for (int i = 0; i < static_cast<int>(controls.size()); ++i) {
                const bool selectedItem = (i == panel_.selectedParameterIndex);
                if (ImGui::Selectable(controls[static_cast<std::size_t>(i)].name.c_str(), selectedItem)) {
                    panel_.selectedParameterIndex = i;
                    panel_.parameterValue = controls[static_cast<std::size_t>(i)].value;
                    std::snprintf(panel_.selectedParameterName, sizeof(panel_.selectedParameterName), "%s", controls[static_cast<std::size_t>(i)].name.c_str());
                    panel_.parameterValueDirty = false;
                }
            }
            ImGui::EndCombo();
        }

        const auto& active = controls[static_cast<std::size_t>(panel_.selectedParameterIndex)];
        if (sliderFloatWithHint("Parameter value", &panel_.parameterValue, active.minValue, active.maxValue, "%.5f",
            "Applies a deterministic global forcing update by queueing input patches for the target field.")) {
            panel_.parameterValueDirty = true;
        }
        if (panel_.parameterValueDirty) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Status: pending*");
        } else {
            ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Status: applied");
        }
        ImGui::TextDisabled("Target field: %s  | units: %s", active.targetVariable.c_str(), active.units.c_str());

        if (liveApplyWhileRunning && panel_.parameterValueDirty) {
            std::string liveMsg;
            if (runtime_.setParameterValue(active.name, panel_.parameterValue, "ui_parameter_live", liveMsg)) {
                panel_.parameterValueDirty = false;
                appendUserFacingOperationMessage(liveMsg, "Live parameter update applied", "Observe state response or adjust parameter further.");
            }
        }

        if (PrimaryButton("Apply parameter", ImVec2(-1.0f, 26.0f))) {
            std::string setMsg;
            runtime_.setParameterValue(active.name, panel_.parameterValue, "ui_parameter_update", setMsg);
            appendUserFacingOperationMessage(setMsg, "Parameter update requested", "Confirm expected field response and continue tuning.");
            panel_.parameterValueDirty = false;
        }
    } else {
        ImGui::TextDisabled("No writable parameter controls are registered.");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual cell/global edit");

    if (!viz_.fieldNames.empty()) {
        const char* manualPatchPreview = panel_.manualPatchVariable[0] != '\0' ? panel_.manualPatchVariable : "<select variable>";
        if (ImGui::BeginCombo("Variable##manualPatch", manualPatchPreview)) {
            for (const auto& fieldName : viz_.fieldNames) {
                const bool selected = (fieldName == std::string(panel_.manualPatchVariable));
                if (ImGui::Selectable(fieldName.c_str(), selected)) {
                    std::snprintf(panel_.manualPatchVariable, sizeof(panel_.manualPatchVariable), "%s", fieldName.c_str());
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::InputText("Variable##manualPatchInput", panel_.manualPatchVariable, sizeof(panel_.manualPatchVariable));
    }

    checkboxWithHint("Global edit (entire field)", &panel_.manualPatchGlobal,
        "When enabled, applies the value to every cell in the target field. When disabled, only one cell is edited.");

    if (!panel_.manualPatchGlobal) {
        NumericSliderPairInt("Cell X", &panel_.manualPatchX, 0, std::max(0, panel_.gridWidth - 1), "%d", 55.0f);
        NumericSliderPairInt("Cell Y", &panel_.manualPatchY, 0, std::max(0, panel_.gridHeight - 1), "%d", 55.0f);
    }
    sliderFloatWithHint("New value", &panel_.manualPatchValue, -10.0f, 10.0f, "%.5f",
        "Manual patch value. Domain validation still applies in runtime write path.");
    inputTextWithHint("Note##manualPatchNote", panel_.manualPatchNote, sizeof(panel_.manualPatchNote),
        "Optional note stored in event log (audit trail).");

    if (PrimaryButton("Apply manual edit", ImVec2(-1.0f, 26.0f))) {
        std::optional<Cell> cell;
        if (!panel_.manualPatchGlobal) {
            cell = Cell{static_cast<std::uint32_t>(std::max(0, panel_.manualPatchX)), static_cast<std::uint32_t>(std::max(0, panel_.manualPatchY))};
        }
        std::string patchMsg;
        const bool patched = runtime_.applyManualPatch(
            panel_.manualPatchVariable,
            cell,
            panel_.manualPatchValue,
            panel_.manualPatchNote,
            patchMsg);
        appendUserFacingOperationMessage(
            patchMsg,
            "Manual field edit requested",
            patched ? "Inspect state diff or continue with additional edits." : "Verify variable/target bounds and retry edit.");
        if (patched) {
            requestSnapshotRefresh();
        }
    }

    if (SecondaryButton("Undo last manual edit", ImVec2(-1.0f, 24.0f))) {
        std::string undoMsg;
        const bool undone = runtime_.undoLastManualPatch(undoMsg);
        appendUserFacingOperationMessage(
            undoMsg,
            "Undo last manual edit requested",
            undone ? "Validate rollback and proceed." : "No undo entry found; apply edits first.");
        if (undone) {
            requestSnapshotRefresh();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Parameter presets");
    inputTextWithHint("Preset name", panel_.parameterPresetName, sizeof(panel_.parameterPresetName),
        "Preset metadata key.");
    const auto presetPath = (std::filesystem::path("profiles") / "parameter_presets" / (std::string(panel_.parameterPresetName) + ".json"));
    if (PrimaryButton("Save preset", ImVec2(140.0f, 24.0f))) {
        ParameterPreset preset;
        preset.name = panel_.parameterPresetName;
        preset.purpose = "runtime_parameter_control";
        preset.date = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        preset.parameters = controls;
        std::string presetMsg;
        const bool saved = saveParameterPreset(preset, presetPath, presetMsg);
        if (!saved && presetMsg.empty()) {
            presetMsg = "parameter_preset_save_failed reason=unknown";
        }
        appendUserFacingOperationMessage(
            presetMsg,
            "Parameter preset save requested",
            saved ? "Load this preset later to restore parameter values." : "Check preset path/name and retry save.");
    }
    ImGui::SameLine();
    if (SecondaryButton("Load preset", ImVec2(140.0f, 24.0f))) {
        ParameterPreset preset;
        std::string presetMsg;
        if (loadParameterPreset(presetPath, preset, presetMsg)) {
            appendUserFacingOperationMessage(presetMsg, "Parameter preset loaded", "Apply loaded values and inspect runtime response.");
            for (const auto& parameter : preset.parameters) {
                std::string setMsg;
                runtime_.setParameterValue(parameter.name, parameter.value, "preset_load", setMsg);
                appendUserFacingOperationMessage(setMsg, "Preset parameter applied", "Continue through preset sequence or tune manually.");
            }
        } else {
            appendUserFacingOperationMessage(presetMsg, "Parameter preset load failed", "Validate preset file schema and retry.");
        }
    }
    ImGui::SameLine();
    if (SecondaryButton("Browse preset...", ImVec2(150.0f, 24.0f))) {
        if (const auto presetFile = ::ws::gui::platform_dialogs::pickNativeFilePath(
                L"Load Parameter Preset",
                L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0\0",
                presetPath,
                false)) {
            std::snprintf(panel_.parameterPresetName, sizeof(panel_.parameterPresetName), "%s", presetFile->stem().string().c_str());
            ParameterPreset preset;
            std::string presetMsg;
            if (loadParameterPreset(*presetFile, preset, presetMsg)) {
                appendUserFacingOperationMessage(presetMsg, "Parameter preset loaded", "Apply loaded values and inspect runtime response.");
                for (const auto& parameter : preset.parameters) {
                    std::string setMsg;
                    runtime_.setParameterValue(parameter.name, parameter.value, "preset_load", setMsg);
                    appendUserFacingOperationMessage(setMsg, "Preset parameter applied", "Continue through preset sequence or tune manually.");
                }
            } else {
                appendUserFacingOperationMessage(presetMsg, "Parameter preset load failed", "Validate selected file and retry.");
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual event log");
    static std::vector<ManualEventRecord> loadedReplayEvents;
    static std::string loadedReplaySource;
    inputTextWithHint("Event log file", panel_.eventLogFileName, sizeof(panel_.eventLogFileName),
        "JSON file under checkpoints/event_logs/.");
    if (SecondaryButton("Export event log", ImVec2(140.0f, 24.0f))) {
        std::vector<ManualEventRecord> events;
        std::string eventMsg;
        if (runtime_.manualEventLog(events, eventMsg)) {
            const auto outputPath = std::filesystem::path("checkpoints") / "event_logs" / panel_.eventLogFileName;
            const bool saved = saveManualEventLog(events, outputPath, eventMsg);
            if (!saved && eventMsg.empty()) {
                eventMsg = "event_log_save_failed reason=unknown";
            }
        }
        appendUserFacingOperationMessage(
            eventMsg,
            "Manual event log export requested",
            "Use exported log for replay preflight or audit trace review.");
    }
    ImGui::SameLine();
    if (SecondaryButton("Load event log", ImVec2(140.0f, 24.0f))) {
        std::string eventMsg;
        const auto inputPath = std::filesystem::path("checkpoints") / "event_logs" / panel_.eventLogFileName;
        if (loadManualEventLog(inputPath, loadedReplayEvents, eventMsg)) {
            loadedReplaySource = inputPath.string();
        } else {
            loadedReplayEvents.clear();
            loadedReplaySource.clear();
        }
        appendUserFacingOperationMessage(
            eventMsg,
            "Manual event log load requested",
            "Run replay preflight to inspect compatibility before execution.");
    }
    ImGui::SameLine();
    if (SecondaryButton("Browse event log...", ImVec2(160.0f, 24.0f))) {
        const auto defaultEventPath = std::filesystem::path("checkpoints") / "event_logs" / panel_.eventLogFileName;
        if (const auto eventFile = ::ws::gui::platform_dialogs::pickNativeFilePath(
                L"Open Event Log",
                L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0\0",
                defaultEventPath,
                false)) {
            std::snprintf(panel_.eventLogFileName, sizeof(panel_.eventLogFileName), "%s", eventFile->filename().string().c_str());
            std::string eventMsg;
            if (loadManualEventLog(*eventFile, loadedReplayEvents, eventMsg)) {
                loadedReplaySource = eventFile->string();
            } else {
                loadedReplayEvents.clear();
                loadedReplaySource.clear();
            }
            appendUserFacingOperationMessage(
                eventMsg,
                "Manual event log load requested",
                "Run replay preflight to inspect compatibility before execution.");
        }
    }

    ImGui::SameLine();
    if (SecondaryButton("Browse export...", ImVec2(150.0f, 24.0f))) {
        std::vector<ManualEventRecord> events;
        std::string eventMsg;
        if (runtime_.manualEventLog(events, eventMsg)) {
            const auto defaultEventPath = std::filesystem::path("checkpoints") / "event_logs" / panel_.eventLogFileName;
            if (const auto eventFile = ::ws::gui::platform_dialogs::pickNativeFilePath(
                    L"Save Event Log",
                    L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0\0",
                    defaultEventPath,
                    true)) {
                std::snprintf(panel_.eventLogFileName, sizeof(panel_.eventLogFileName), "%s", eventFile->filename().string().c_str());
                const bool saved = saveManualEventLog(events, *eventFile, eventMsg);
                if (!saved && eventMsg.empty()) {
                    eventMsg = "event_log_save_failed reason=unknown";
                }
            }
        }
        appendUserFacingOperationMessage(
            eventMsg,
            "Manual event log export requested",
            "Use exported log for replay preflight or archive workflows.");
    }

    std::vector<ParameterControl> replayParameterControls;
    std::string replayControlsMsg;
    runtime_.parameterControls(replayParameterControls, replayControlsMsg);

    static int replayExecutionModeIndex = 0;
    static constexpr std::array<const char*, 2> kReplayExecutionModes = {
        "Replay only safe subset",
        "Stop on first unresolved critical event"
    };

    const auto parameterResolutionForEvent = [&replayParameterControls](const ManualEventRecord& event)
        -> std::pair<std::optional<std::string>, std::string> {
        if (event.description.rfind("parameter=", 0) == 0) {
            const std::string candidate = event.description.substr(std::string("parameter=").size());
            if (!candidate.empty()) {
                return {candidate, "direct"};
            }
        }
        for (const auto& control : replayParameterControls) {
            if (control.targetVariable == event.variable) {
                return {control.name, "mapped"};
            }
        }
        return {std::nullopt, "unresolved"};
    };

    std::size_t replaySupportedCount = 0;
    std::size_t replaySkippedKindCount = 0;
    std::size_t replayUnresolvedParameterCount = 0;
    std::size_t replayCriticalUnresolvedCount = 0;
    for (const auto& event : loadedReplayEvents) {
        if (event.kind == ManualEventKind::CellEdit) {
            ++replaySupportedCount;
            continue;
        }
        if (event.kind != ManualEventKind::ParameterUpdate) {
            ++replaySkippedKindCount;
            continue;
        }

        const auto [resolvedParameter, resolutionSource] = parameterResolutionForEvent(event);
        const bool parameterResolved = resolvedParameter.has_value() && resolutionSource != "unresolved";

        if (parameterResolved) {
            ++replaySupportedCount;
        } else {
            ++replayUnresolvedParameterCount;
            ++replayCriticalUnresolvedCount;
        }
    }

    const std::size_t replayPlanTotal = loadedReplayEvents.size();
    const std::size_t replayPlanCompatible = replaySupportedCount;
    const std::size_t replayPlanUnsafe = replayPlanTotal >= replayPlanCompatible
        ? (replayPlanTotal - replayPlanCompatible)
        : 0u;
    const float replayCompatibilityScore = replayPlanTotal > 0u
        ? (100.0f * static_cast<float>(replayPlanCompatible) / static_cast<float>(replayPlanTotal))
        : 0.0f;

    if (!loadedReplayEvents.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Replay preflight");
        ImGui::TextWrapped(
            "Loaded %zu event(s): %zu replayable, %zu skipped by kind, %zu unresolved parameters.",
            loadedReplayEvents.size(),
            replaySupportedCount,
            replaySkippedKindCount,
            replayUnresolvedParameterCount);
        ImGui::TextDisabled(
            "Deterministic compatibility score: %.1f%% (%zu/%zu replayable)",
            replayCompatibilityScore,
            replayPlanCompatible,
            replayPlanTotal);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(
            "Replay execution mode",
            &replayExecutionModeIndex,
            kReplayExecutionModes.data(),
            static_cast<int>(kReplayExecutionModes.size()));
        ImGui::TextDisabled(
            "Current mode: %s",
            kReplayExecutionModes[static_cast<std::size_t>(std::clamp(replayExecutionModeIndex, 0, static_cast<int>(kReplayExecutionModes.size()) - 1))]);

        if (ImGui::BeginTable(
            "ReplayExecutionPlan",
            6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, 170.0f))) {
            ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Expected side effect");
            ImGui::TableSetupColumn("Critical", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            const auto planKindLabel = [](const ManualEventKind kind) {
                switch (kind) {
                    case ManualEventKind::ParameterUpdate: return "Parameter";
                    case ManualEventKind::CellEdit: return "Cell edit";
                    case ManualEventKind::Perturbation: return "Perturbation";
                }
                return "Manual";
            };

            const std::size_t planPreviewCount = std::min<std::size_t>(loadedReplayEvents.size(), 24u);
            for (std::size_t index = 0; index < planPreviewCount; ++index) {
                const auto& event = loadedReplayEvents[index];
                const auto [resolvedParameter, resolutionSource] = parameterResolutionForEvent(event);

                std::string status = "replayable";
                std::string expectedEffect;
                bool criticalUnresolved = false;
                if (event.kind == ManualEventKind::CellEdit) {
                    expectedEffect = (event.cellIndex == std::numeric_limits<std::uint64_t>::max())
                        ? "global cell patch"
                        : "single-cell patch";
                } else if (event.kind == ManualEventKind::ParameterUpdate) {
                    if (resolvedParameter.has_value() && resolutionSource != "unresolved") {
                        expectedEffect = "parameter write " + *resolvedParameter + " -> " + event.variable;
                    } else {
                        status = "blocked";
                        expectedEffect = "parameter mapping unresolved";
                        criticalUnresolved = true;
                    }
                } else {
                    status = "skipped";
                    expectedEffect = "unsupported event kind";
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", static_cast<unsigned long long>(event.step));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(planKindLabel(event.kind));
                ImGui::TableSetColumnIndex(2);
                if (status == "replayable") {
                    ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "%s", status.c_str());
                } else if (status == "skipped") {
                    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "%s", status.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", status.c_str());
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(resolutionSource.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(expectedEffect.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(criticalUnresolved ? "yes" : "no");
            }
            ImGui::EndTable();
        }

        if (replayPlanUnsafe > 0u) {
            ImGui::TextDisabled(
                "Plan summary: %zu safe event%s, %zu unsafe/skipped event%s, %zu critical unresolved.",
                replayPlanCompatible,
                replayPlanCompatible == 1u ? "" : "s",
                replayPlanUnsafe,
                replayPlanUnsafe == 1u ? "" : "s",
                replayCriticalUnresolvedCount);
        }
        if (!runtime_.isRunning()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Start a simulation to run this replay experiment.");
        } else if (!runtime_.isPaused()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Pause the simulation to apply replay events deterministically.");
        } else {
            ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Runtime is paused and ready for replay.");
        }

        if (SecondaryButton("Capture baseline checkpoint for replay", ImVec2(-1.0f, 24.0f))) {
            const std::uint64_t step = viz_.hasCachedCheckpoint
                ? viz_.cachedCheckpoint.stateSnapshot.header.stepIndex
                : 0u;
            const std::string label = "replay_baseline_step_" + std::to_string(step);
            std::string checkpointMsg;
            [[maybe_unused]] const bool created = runtime_.createCheckpoint(label, checkpointMsg);
            appendUserFacingOperationMessage(
                checkpointMsg,
                "Replay baseline checkpoint requested",
                "Use restore/compare workflows after replay execution.");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Creates an in-memory baseline before replay so you can compare or restore quickly.");
        }
    }

    const bool canReplayLoadedEvents =
        runtime_.isRunning() &&
        runtime_.isPaused() &&
        !loadedReplayEvents.empty();
    if (SecondaryButton("Replay compatible entries", ImVec2(-1.0f, 24.0f))) {
        if (!runtime_.isRunning()) {
            appendUserFacingOperationMessage(
                "event_log_replay_failed reason=runtime_not_running",
                "Replay request rejected",
                "Start a runtime, pause it, then rerun replay.");
        } else if (!runtime_.isPaused()) {
            appendUserFacingOperationMessage(
                "event_log_replay_failed reason=runtime_not_paused",
                "Replay request rejected",
                "Pause runtime to preserve deterministic replay ordering.");
        } else {
            if (loadedReplayEvents.empty()) {
                std::string eventMsg;
                const auto inputPath = std::filesystem::path("checkpoints") / "event_logs" / panel_.eventLogFileName;
                if (loadManualEventLog(inputPath, loadedReplayEvents, eventMsg)) {
                    loadedReplaySource = inputPath.string();
                }
                appendUserFacingOperationMessage(
                    eventMsg,
                    "Manual event log load requested",
                    "Run replay preflight and execute compatible entries.");
            }

            if (!loadedReplayEvents.empty()) {
                std::vector<ManualEventRecord> replayEvents = loadedReplayEvents;
                std::stable_sort(
                    replayEvents.begin(),
                    replayEvents.end(),
                    [](const ManualEventRecord& lhs, const ManualEventRecord& rhs) {
                        if (lhs.step != rhs.step) {
                            return lhs.step < rhs.step;
                        }
                        return lhs.timestamp < rhs.timestamp;
                    });

                std::vector<ParameterControl> controls;
                std::string controlsMsg;
                runtime_.parameterControls(controls, controlsMsg);

                const std::uint64_t gridWidth = viz_.hasCachedCheckpoint
                    ? static_cast<std::uint64_t>(std::max(1u, viz_.cachedCheckpoint.stateSnapshot.grid.width))
                    : static_cast<std::uint64_t>(std::max(1, panel_.gridWidth));

                const auto parameterNameForEvent = [&controls](const ManualEventRecord& event)
                    -> std::pair<std::optional<std::string>, std::string> {
                    if (event.description.rfind("parameter=", 0) == 0) {
                        const std::string candidate = event.description.substr(std::string("parameter=").size());
                        if (!candidate.empty()) {
                            return {candidate, "direct"};
                        }
                    }
                    for (const auto& control : controls) {
                        if (control.targetVariable == event.variable) {
                            return {control.name, "mapped"};
                        }
                    }
                    return {std::nullopt, "unresolved"};
                };

                std::size_t appliedCount = 0;
                std::size_t skippedCount = 0;
                std::size_t jumpCount = 0;
                std::size_t criticalAbortCount = 0;
                std::uint64_t replayStep = viz_.hasCachedCheckpoint
                    ? viz_.cachedCheckpoint.stateSnapshot.header.stepIndex
                    : 0u;
                const bool stopOnCriticalUnresolved = replayExecutionModeIndex == 1;

                for (const auto& event : replayEvents) {
                    if (event.step != replayStep) {
                        if (!seekToStepAndRefresh(event.step)) {
                            ++skippedCount;
                            continue;
                        }
                        replayStep = event.step;
                        ++jumpCount;
                    }

                    std::string replayMsg;
                    bool applied = false;
                    if (event.kind == ManualEventKind::ParameterUpdate) {
                        const auto [parameterName, resolutionSource] = parameterNameForEvent(event);
                        if (parameterName.has_value()) {
                            applied = runtime_.setParameterValue(
                                *parameterName,
                                event.newValue,
                                "event_log_replay",
                                replayMsg);
                        } else {
                            replayMsg = "event_log_replay_skipped reason=parameter_unresolved variable=" + event.variable;
                            if (stopOnCriticalUnresolved) {
                                ++criticalAbortCount;
                                appendUserFacingOperationMessage(
                                    replayMsg,
                                    "Replay entry unresolved",
                                    "Resolve parameter mapping and rerun replay plan.");
                                appendUserFacingOperationMessage(
                                    "event_log_replay_aborted reason=critical_unresolved_parameter mode=stop_on_first",
                                    "Replay aborted on first unresolved critical event",
                                    "Inspect execution plan and map unresolved parameter sources before retry.");
                                break;
                            }
                        }
                    } else if (event.kind == ManualEventKind::CellEdit) {
                        std::optional<Cell> replayCell;
                        if (event.cellIndex != std::numeric_limits<std::uint64_t>::max()) {
                            replayCell = Cell{
                                static_cast<std::uint32_t>(event.cellIndex % gridWidth),
                                static_cast<std::uint32_t>(event.cellIndex / gridWidth)};
                        }
                        applied = runtime_.applyManualPatch(
                            event.variable,
                            replayCell,
                            event.newValue,
                            "event_log_replay",
                            replayMsg);
                    } else {
                        replayMsg = "event_log_replay_skipped reason=unsupported_kind kind=perturbation variable=" + event.variable;
                    }

                    appendUserFacingOperationMessage(
                        replayMsg,
                        "Replay event applied",
                        "Continue replay or inspect updated state and traces.");
                    if (applied) {
                        ++appliedCount;
                    } else {
                        ++skippedCount;
                    }
                }

                requestSnapshotRefresh();
                appendUserFacingOperationMessage(
                    "event_log_replay_complete source=" +
                    (loadedReplaySource.empty() ? std::string("<memory>") : loadedReplaySource) +
                    " applied=" + std::to_string(appliedCount) +
                    " skipped=" + std::to_string(skippedCount) +
                    " seeks=" + std::to_string(jumpCount) +
                    " critical_abort=" + std::to_string(criticalAbortCount) +
                    " mode=" + std::string(stopOnCriticalUnresolved ? "stop_on_first_unresolved" : "safe_subset") +
                    " loaded=" + std::to_string(replayEvents.size()),
                    "Replay execution finished",
                    stopOnCriticalUnresolved
                        ? "Review unresolved critical event and fix mapping before rerun."
                        : "Compare replay result to baseline checkpoint and continue analysis.");
            }
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !canReplayLoadedEvents) {
        ImGui::SetTooltip("Requires a loaded event log and a paused simulation.\nModes: replay safe subset, or stop on first unresolved critical event.");
    }

    if (!loadedReplayEvents.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Loaded replay source: %s (%zu event%s)",
            loadedReplaySource.empty() ? "<memory>" : loadedReplaySource.c_str(),
            loadedReplayEvents.size(),
            loadedReplayEvents.size() == 1u ? "" : "s");
        if (ImGui::BeginTable(
            "LoadedManualEventPreview",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, 150.0f))) {
            ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Variable");
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableHeadersRow();

            const auto kindLabel = [](const ManualEventKind kind) {
                switch (kind) {
                    case ManualEventKind::ParameterUpdate: return "Parameter";
                    case ManualEventKind::CellEdit: return "Cell edit";
                    case ManualEventKind::Perturbation: return "Perturbation";
                }
                return "Manual";
            };

            const std::size_t previewCount = std::min<std::size_t>(loadedReplayEvents.size(), 20u);
            for (std::size_t offset = 0; offset < previewCount; ++offset) {
                const auto& event = loadedReplayEvents[offset];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", static_cast<unsigned long long>(event.step));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(kindLabel(event.kind));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(event.variable.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.5f", event.newValue);
                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(static_cast<int>(offset));
                if (ImGui::SmallButton("Jump")) {
                    seekToStepAndRefresh(event.step);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Recent manual changes", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::vector<ManualEventRecord> events;
        std::string eventMsg;
        if (runtime_.manualEventLog(events, eventMsg)) {
            const auto kindLabel = [](const ManualEventKind kind) {
                switch (kind) {
                    case ManualEventKind::ParameterUpdate: return "Parameter";
                    case ManualEventKind::CellEdit: return "Cell edit";
                    case ManualEventKind::Perturbation: return "Perturbation";
                }
                return "Manual";
            };

            const std::uint64_t gridWidth = viz_.hasCachedCheckpoint
                ? static_cast<std::uint64_t>(std::max(1u, viz_.cachedCheckpoint.stateSnapshot.grid.width))
                : static_cast<std::uint64_t>(std::max(1, panel_.gridWidth));

            const auto targetLabel = [gridWidth](const ManualEventRecord& event) {
                if (event.cellIndex == std::numeric_limits<std::uint64_t>::max()) {
                    return std::string("global");
                }

                const std::uint64_t x = event.cellIndex % gridWidth;
                const std::uint64_t y = event.cellIndex / gridWidth;
                return std::string("(") + std::to_string(x) + ", " + std::to_string(y) + ")";
            };

            if (events.empty()) {
                ImGui::TextDisabled("No manual edits or perturbations have been recorded yet.");
            } else if (ImGui::BeginTable(
                "RecentManualChanges",
                7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, 220.0f))) {
                ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Variable");
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Change");
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableHeadersRow();

                const std::size_t displayCount = std::min<std::size_t>(events.size(), 32u);
                for (std::size_t offset = 0; offset < displayCount; ++offset) {
                    const auto& event = events[events.size() - 1u - offset];
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", static_cast<unsigned long long>(event.step));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", event.time);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(kindLabel(event.kind));

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(event.variable.c_str());

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(targetLabel(event).c_str());

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.5f -> %.5f", event.oldValue, event.newValue);
                    if (!event.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("%s", event.description.c_str());
                    }

                    ImGui::TableSetColumnIndex(6);
                    ImGui::PushID(static_cast<int>(offset));
                    if (ImGui::SmallButton("Jump")) {
                        seekToStepAndRefresh(event.step);
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("%s", eventMsg.c_str());
        }
    }
}

// Draws perturbation section with field modifications.
void drawPerturbationSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to queue perturbations.");
        return;
    }

    ImGui::TextDisabled("Change scope");
    ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Scheduled event");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Applies at configured step window");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Persists after Save Active World");
    ImGui::TextDisabled("Perturbations are queued. They do not mutate state immediately until start step is reached.");

    static constexpr const char* kPerturbationTypes[] = {
        "Gaussian pulse", "Rectangular region", "Sine wave", "White noise", "Gradient"};
    ImGui::Combo("Perturbation type", &panel_.perturbationTypeIndex, kPerturbationTypes, static_cast<int>(std::size(kPerturbationTypes)));

    if (!viz_.fieldNames.empty()) {
        const char* perturbPreview = panel_.perturbationVariable[0] != '\0' ? panel_.perturbationVariable : "<select variable>";
        if (ImGui::BeginCombo("Target variable##perturb", perturbPreview)) {
            for (const auto& fieldName : viz_.fieldNames) {
                const bool selected = (fieldName == std::string(panel_.perturbationVariable));
                if (ImGui::Selectable(fieldName.c_str(), selected)) {
                    std::snprintf(panel_.perturbationVariable, sizeof(panel_.perturbationVariable), "%s", fieldName.c_str());
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::InputText("Target variable##perturbInput", panel_.perturbationVariable, sizeof(panel_.perturbationVariable));
    }

    sliderFloatWithHint("Amplitude", &panel_.perturbationAmplitude, -1.0f, 1.0f, "%.4f",
        "Perturbation amplitude (domain clamping still enforced by core guardrails). ");
    NumericSliderPairInt("Start step offset", &panel_.perturbationStartStepOffset, 0, 1000000, "%d", 55.0f);
    NumericSliderPairInt("Duration steps", &panel_.perturbationDuration, 1, 1000000, "%d", 55.0f);
    NumericSliderPairInt("Origin X", &panel_.perturbationOriginX, 0, std::max(0, panel_.gridWidth - 1), "%d", 55.0f);
    NumericSliderPairInt("Origin Y", &panel_.perturbationOriginY, 0, std::max(0, panel_.gridHeight - 1), "%d", 55.0f);
    NumericSliderPairInt("Width", &panel_.perturbationWidth, 1, std::max(1, panel_.gridWidth), "%d", 55.0f);
    NumericSliderPairInt("Height", &panel_.perturbationHeight, 1, std::max(1, panel_.gridHeight), "%d", 55.0f);
    sliderFloatWithHint("Sigma", &panel_.perturbationSigma, 0.5f, 32.0f, "%.3f",
        "Gaussian sigma (used by Gaussian perturbation type).");
    sliderFloatWithHint("Frequency", &panel_.perturbationFrequency, 0.01f, 4.0f, "%.3f",
        "Spatial frequency (used by Sine perturbation type).");
    inputTextWithHint("Note##perturbNote", panel_.perturbationNote, sizeof(panel_.perturbationNote),
        "Structured note stored in manual event log.");

    PerturbationSpec spec;
    spec.type = static_cast<PerturbationType>(std::clamp(panel_.perturbationTypeIndex, 0, 4));
    spec.targetVariable = panel_.perturbationVariable;
    spec.amplitude = panel_.perturbationAmplitude;
    spec.startStep = static_cast<std::uint32_t>(std::max(0, panel_.perturbationStartStepOffset));
    if (viz_.hasCachedCheckpoint) {
        spec.startStep += static_cast<std::uint32_t>(viz_.cachedCheckpoint.stateSnapshot.header.stepIndex);
    }
    spec.durationSteps = static_cast<std::uint32_t>(std::max(1, panel_.perturbationDuration));
    spec.origin = Cell{static_cast<std::uint32_t>(std::max(0, panel_.perturbationOriginX)), static_cast<std::uint32_t>(std::max(0, panel_.perturbationOriginY))};
    spec.width = static_cast<std::uint32_t>(std::max(1, panel_.perturbationWidth));
    spec.height = static_cast<std::uint32_t>(std::max(1, panel_.perturbationHeight));
    spec.sigma = panel_.perturbationSigma;
    spec.frequency = panel_.perturbationFrequency;
    spec.noiseSeed = panel_.perturbationNoiseSeed;
    spec.description = panel_.perturbationNote;

    std::string validateMsg;
    const GridSpec grid{
        static_cast<std::uint32_t>(std::max(1, panel_.gridWidth)),
        static_cast<std::uint32_t>(std::max(1, panel_.gridHeight))};
    const bool valid = validatePerturbation(spec, grid, validateMsg);
    const auto impactedCells = estimatePerturbationCellCount(spec, grid);
    ImGui::TextDisabled("Preview: estimated impacted cells=%llu", static_cast<unsigned long long>(impactedCells));
    if (!valid) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", validateMsg.c_str());
    }

    if (PrimaryButton("Apply perturbation", ImVec2(-1.0f, 28.0f))) {
        std::string applyMsg;
        if (valid) {
            runtime_.enqueuePerturbation(spec, applyMsg);
        } else {
            applyMsg = validateMsg;
        }
        appendUserFacingOperationMessage(
            applyMsg,
            "Perturbation enqueue requested",
            valid ? "Step simulation to observe perturbation effects." : "Correct perturbation bounds/settings and retry.");
    }
}

// Draws tier selector for simulation mode.
void drawTierSelector() {
    const bool isRunning = runtime_.isRunning();

    ImGui::TextDisabled("Change scope");
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Restart required");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Saved with world when you save");

    // Current profile badges + description
    static constexpr const char* kTierDesc[3] = {
        "Local deterministic updates with no neighborhood exchange.\n"
        "Best for fast iteration and strict reproducibility.",
        "Adds neighborhood exchange and staged update flow.\n"
        "Balances throughput with richer local coupling.",
        "Uses adaptive multi-rate updates and stronger coupling paths.\n"
        "Highest computational cost with the richest dynamics."
    };
    static constexpr ImVec4 kTierColors[3] = {
        {0.35f, 0.75f, 0.45f, 1.0f},
        {0.75f, 0.75f, 0.25f, 1.0f},
        {0.85f, 0.45f, 0.25f, 1.0f}
    };

    const int currentTier = panel_.tierIndex;
    ImGui::TextColored(kTierColors[currentTier], "Active profile: %s",
        kTierOptions[currentTier]);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", kTierDesc[currentTier]);

    if (isRunning) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
        ImGui::TextWrapped("Warning: profile changes take effect after you restart the simulation using the button below.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Profile radio buttons
    for (int i = 0; i < 3; ++i) {
        const bool sel = (panel_.tierIndex == i);
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? kTierColors[i] : ImVec4(0.7f,0.7f,0.7f,1.0f));
        if (ImGui::RadioButton(kTierOptions[i], sel)) {
            panel_.tierIndex = i;
            // auto-match recommended temporal behavior
            const ws::TemporalPolicy recommended =
                (i == 0) ? ws::TemporalPolicy::UniformA :
                (i == 1) ? ws::TemporalPolicy::PhasedB  : ws::TemporalPolicy::MultiRateC;
            const std::string recStr = app::temporalPolicyToString(recommended);
            for (int j = 0; j < 3; ++j)
                if (std::string(kTemporalPolicyTokens[j]) == recStr) { panel_.temporalIndex = j; break; }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", kTierDesc[i]);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Temporal behavior:");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##temporal", kTemporalOptions[panel_.temporalIndex])) {
        for (int i = 0; i < 3; ++i) {
            bool sel = (panel_.temporalIndex == i);
            if (ImGui::Selectable(kTemporalOptions[i], sel)) panel_.temporalIndex = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(
            "Single-pass        - one ordered pass per step\n"
            "Phased             - staged execution across subsystem groups\n"
            "Adaptive multi-rate - micro-stepped execution with adaptive sub-iterations\n"
            "The selected behavior must be compatible with the selected execution profile.");

    // Constraint reminder
    {
        const ws::TemporalPolicy req =
            (panel_.tierIndex == 0) ? ws::TemporalPolicy::UniformA :
            (panel_.tierIndex == 1) ? ws::TemporalPolicy::PhasedB  : ws::TemporalPolicy::MultiRateC;
        const auto selected = app::parseTemporalPolicy(kTemporalPolicyTokens[panel_.temporalIndex]);
        if (selected.has_value() && *selected != req) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("Warning: temporal behavior mismatch - admission will reject this combination.");
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    if (isRunning) {
        if (PrimaryButton("Restart with these settings", ImVec2(-1.0f, 30.0f))) {
            applyConfigFromPanel();
            std::string msg;
            if (runtime_.restart(msg)) {
                viz_.autoRun = false;
                appendUserFacingOperationMessage(msg, "Runtime restart requested", "Validate updated profile/temporal behavior before resuming.");
                syncPanelFromConfig();
                requestSnapshotRefresh();
                enterSimulationPaused();
            } else {
                appendUserFacingOperationMessage(msg, "Runtime restart failed", "Correct profile/temporal mismatch and retry restart.");
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Restarts the simulation with updated profile and temporal behavior settings.\nThe simulation will be paused after restart.");
    } else {
        if (PrimaryButton("Apply these settings", ImVec2(-1.0f, 30.0f))) {
            applyConfigFromPanel();
            appendUserFacingOperationMessage(
                "settings_applied profile_temporal",
                "Execution profile and temporal behavior settings applied",
                "Start simulation to run with the selected settings.");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Applies profile/temporal settings without starting the simulation.\nUse 'Start Simulation' button to begin.");
    }
}

// Draws playback controls (play/pause/stop).
void drawPlaybackSection() {
    const bool running = runtime_.isRunning();
    const bool paused  = runtime_.isPaused();

    drawRuntimeIntentControls("playback");

    std::size_t pendingImmediateWrites = 0;
    std::size_t queuedDeferredEvents = 0;
    std::size_t pendingScheduledPerturbations = 0;
    std::size_t runtimeManualEventCount = 0;
    std::string effectLedgerMessage;
    bool pendingRestart = false;

    if (running) {
        [[maybe_unused]] const bool ledgerRead = runtime_.effectLedgerCounts(
            pendingImmediateWrites,
            queuedDeferredEvents,
            pendingScheduledPerturbations,
            runtimeManualEventCount,
            effectLedgerMessage);

        const auto& cfg = runtime_.config();
        const int runtimeTierIndex =
            (cfg.tier == ModelTier::A) ? 0 :
            (cfg.tier == ModelTier::B) ? 1 : 2;
        const std::string runtimeTemporal = app::temporalPolicyToString(cfg.temporalPolicy);
        const int runtimeTemporalIndex =
            (runtimeTemporal == "uniform") ? 0 :
            (runtimeTemporal == "phased") ? 1 : 2;
        pendingRestart =
            runtimeTierIndex != panel_.tierIndex ||
            runtimeTemporalIndex != panel_.temporalIndex;
    }

    const bool hasUnsavedRuntimeChanges = running && (
        pendingImmediateWrites > 0 ||
        queuedDeferredEvents > 0 ||
        pendingScheduledPerturbations > 0 ||
        runtimeManualEventCount > 0 ||
        pendingRestart);

    ImGui::TextDisabled("Action hierarchy");
    ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Safe read-only");
    ImGui::SameLine();
    ImGui::TextDisabled("status, metrics, field summaries");
    ImGui::TextColored(ImVec4(0.70f, 0.82f, 0.95f, 1.0f), "Reversible");
    ImGui::SameLine();
    ImGui::TextDisabled("pause/resume, step, undo manual edit");
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Destructive / irreversible");
    ImGui::SameLine();
    ImGui::TextDisabled("stop-reset, checkpoint delete, replay commits");
    if (hasUnsavedRuntimeChanges) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.65f, 0.35f, 1.0f),
            "Guard active: destructive actions require explicit save-or-discard choice while unsaved runtime changes exist.");
    }
    ImGui::Spacing();

    if (!running) {
        if (PrimaryButton("Start Simulation", ImVec2(-1.0f, 34.0f))) {
            applyConfigFromPanel();
            std::string msg;
            if (runtime_.start(msg)) {
                viz_.autoRun = true;
                appendUserFacingOperationMessage(msg, "Simulation start requested", "Observe runtime status and proceed with controls/interventions.");
                syncPanelFromConfig();
                refreshFieldNames(); requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Play);
            } else {
                appendUserFacingOperationMessage(msg, "Simulation start failed", "Review runtime readiness and retry start.");
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Initialize and start the simulation from step 0.\nApplies the current execution profile and temporal behavior settings.");
    } else {
        const float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;

        // Play/Pause
        if (paused || !viz_.autoRun) {
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(30, 120, 60, 220));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(45, 155, 80, 240));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(20, 95, 45, 255));
            if (ImGui::Button("Play / Resume [Space]", ImVec2(halfW, 34.0f))) {
                std::string msg; runtime_.resume(msg); viz_.autoRun = true;
                appendUserFacingOperationMessage(msg, "Simulation resumed", "Pause when ready to apply deterministic edits or replay.");
                requestSnapshotRefresh(); triggerOverlay(OverlayIcon::Play);
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(130, 100, 20, 220));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(160, 125, 30, 240));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(100, 80, 15, 255));
            if (ImGui::Button("Pause [Space]", ImVec2(halfW, 34.0f))) {
                cancelPendingSimulationSteps();
                std::string msg; runtime_.pause(msg); viz_.autoRun = false;
                appendUserFacingOperationMessage(msg, "Simulation paused", "Use step, replay, or manual edit controls before resuming.");
                triggerOverlay(OverlayIcon::Pause);
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine();

        // Stop
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(120, 40, 40, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(155, 55, 55, 240));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(95, 30, 30, 255));
        if (ImGui::Button("Stop & Reset", ImVec2(-1.0f, 34.0f))) {
            showStopResetConfirm_ = true;
            ImGui::OpenPopup("Confirm Stop and Reset");
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Stop and terminate the runtime.\nIf unsaved runtime changes exist, a save-or-discard guard is required before stopping.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Auto-run speed controls (only shown when playing)
        if (!paused && viz_.autoRun) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Continuous auto-run");
            checkboxWithHint("Unlimited simulation speed", &viz_.unlimitedSimSpeed,
                "Run the simulation thread at full speed.\n"
                "Disable this to insert a small yield between batches and keep the UI more relaxed.");
            sliderIntWithHint("Target display refresh (Hz)", &viz_.displayTargetRefreshHz, 15, 240,
                "Upper target refresh rate for viewport updates during simulation.\n"
                "Higher values reduce visual latency but can increase snapshot workload.");
            checkboxWithHint("Refresh runtime view on each state change", &viz_.displayRefreshOnStateChange,
                "When enabled, request a display snapshot after each committed simulation batch.\n"
                "Best for continuous visual tracking; disable to maximize raw simulation throughput.");
            sliderIntWithHint("Steps between display updates", &viz_.displayRefreshEveryNSteps, 1, 1000,
                "Refresh the display after every N simulation steps.\n"
                "1 = update every step. Higher values favor simulation throughput over visual refresh rate.\n"
                "A live latency cap still keeps the viewport refreshing regularly during fast auto-run.");
            const float estStepsPerSec = estimatedSimulationStepsPerSecond();
            const float estRefreshesPerSec = estimatedDisplayRefreshesPerSecond();
            const float actualRefreshesPerSec = estimatedActualDisplayRefreshesPerSecond();
            if (estStepsPerSec > 0.0f) {
                ImGui::TextDisabled("Estimated: ~%.0f sim steps/sec, ~%.1f display updates/sec (<= %.0f ms latency)",
                    estStepsPerSec, estRefreshesPerSec, displayRefreshLatencyCapMs());
                if (actualRefreshesPerSec > 0.0f) {
                    ImGui::TextDisabled("Observed runtime-view refresh: %.1f updates/sec", actualRefreshesPerSec);
                }
            } else {
                ImGui::TextDisabled("Estimated: waiting for a completed auto-run batch...");
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Manual / Paused");
            ImGui::TextDisabled("Use Step Controls below, or press Space to resume.");
        }
    }
}

[[nodiscard]] int detectRuntimeIntentFromVisualization() const {
    const bool unlimited = viz_.unlimitedSimSpeed;
    const int refreshHz = viz_.displayTargetRefreshHz;
    const bool refreshOnStateChange = viz_.displayRefreshOnStateChange;
    const int refreshEvery = viz_.displayRefreshEveryNSteps;
    const bool adaptive = viz_.adaptiveSampling;
    const int manualStride = viz_.manualSamplingStride;
    const int maxCells = viz_.maxRenderedCells;

    const bool interactiveExploration =
        !unlimited &&
        refreshHz == 120 &&
        refreshOnStateChange &&
        refreshEvery == 1 &&
        adaptive &&
        maxCells == 350000;

    const bool balanced =
        unlimited &&
        refreshHz == 90 &&
        refreshOnStateChange &&
        refreshEvery == 2 &&
        adaptive &&
        maxCells == 300000;

    const bool throughputBenchmark =
        unlimited &&
        refreshHz == 30 &&
        !refreshOnStateChange &&
        refreshEvery == 24 &&
        adaptive &&
        maxCells == 120000;

    const bool stabilityDiagnostics =
        !unlimited &&
        refreshHz == 120 &&
        refreshOnStateChange &&
        refreshEvery == 1 &&
        !adaptive &&
        manualStride == 1 &&
        maxCells == 600000;

    if (interactiveExploration) return 0;
    if (balanced) return 1;
    if (throughputBenchmark) return 2;
    if (stabilityDiagnostics) return 3;
    return 4;
}

void applyRuntimeIntentPreset(const int intentIndex) {
    switch (intentIndex) {
        case 0: // Interactive exploration
            viz_.unlimitedSimSpeed = false;
            viz_.displayTargetRefreshHz = 120;
            viz_.displayRefreshOnStateChange = true;
            viz_.displayRefreshEveryNSteps = 1;
            viz_.adaptiveSampling = true;
            viz_.maxRenderedCells = 350000;
            break;
        case 1: // Balanced
            viz_.unlimitedSimSpeed = true;
            viz_.displayTargetRefreshHz = 90;
            viz_.displayRefreshOnStateChange = true;
            viz_.displayRefreshEveryNSteps = 2;
            viz_.adaptiveSampling = true;
            viz_.maxRenderedCells = 300000;
            break;
        case 2: // Throughput benchmark
            viz_.unlimitedSimSpeed = true;
            viz_.displayTargetRefreshHz = 30;
            viz_.displayRefreshOnStateChange = false;
            viz_.displayRefreshEveryNSteps = 24;
            viz_.adaptiveSampling = true;
            viz_.maxRenderedCells = 120000;
            break;
        case 3: // Stability diagnostics
            viz_.unlimitedSimSpeed = false;
            viz_.displayTargetRefreshHz = 120;
            viz_.displayRefreshOnStateChange = true;
            viz_.displayRefreshEveryNSteps = 1;
            viz_.adaptiveSampling = false;
            viz_.manualSamplingStride = 1;
            viz_.maxRenderedCells = 600000;
            break;
        default:
            break;
    }
}

void drawRuntimeIntentControls(const char* comboIdSuffix) {
    static constexpr std::array<const char*, 5> kRuntimeIntentLabels = {
        "Interactive exploration",
        "Balanced",
        "Throughput benchmark",
        "Stability diagnostics",
        "Custom"
    };

    const int detectedIntent = detectRuntimeIntentFromVisualization();
    const std::string comboId = std::string("Runtime intent##") + comboIdSuffix;

    ImGui::TextDisabled("Performance intent");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(comboId.c_str(), kRuntimeIntentLabels[static_cast<std::size_t>(detectedIntent)])) {
        for (int i = 0; i < static_cast<int>(kRuntimeIntentLabels.size()); ++i) {
            const bool isSelected = (i == detectedIntent);
            if (ImGui::Selectable(kRuntimeIntentLabels[static_cast<std::size_t>(i)], isSelected)) {
                if (i < 4) {
                    applyRuntimeIntentPreset(i);
                    appendUserFacingOperationMessage(
                        std::string("runtime_intent_applied intent=") + kRuntimeIntentLabels[static_cast<std::size_t>(i)],
                        "Runtime performance intent applied",
                        "Run simulation and confirm throughput/visual responsiveness tradeoff.");
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    switch (detectedIntent) {
        case 0:
            ImGui::TextDisabled("Preset focus: responsive visual exploration with frequent refresh and controlled simulation speed.");
            break;
        case 1:
            ImGui::TextDisabled("Preset focus: balanced simulation throughput and viewport responsiveness for day-to-day runs.");
            break;
        case 2:
            ImGui::TextDisabled("Preset focus: maximize simulation throughput by minimizing display overhead.");
            break;
        case 3:
            ImGui::TextDisabled("Preset focus: high-observability diagnostics with dense display updates and conservative pacing.");
            break;
        default:
            ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.45f, 1.0f), "Custom mode active: manual knob overrides are in effect.");
            ImGui::TextDisabled("Select one of the named intents to restore a known performance bundle.");
            break;
    }

    ImGui::Spacing();
}

// Draws step control buttons.
void drawStepControlsSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to enable step controls.");
        return;
    }

    sliderIntWithHint("Step size", &panel_.stepCount, 1, 100000,
        "Number of steps to advance per manual step press.\nShortcut: right arrow when paused.");

    if (PrimaryButton("Step Forward", ImVec2(-1.0f, 28.0f))) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), msg);
        appendUserFacingOperationMessage(msg, "Step forward requested", "Inspect new state and continue stepping or resume playback.");
        requestSnapshotRefresh();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Advance exactly %d step(s).\nShortcut: right arrow when paused.", panel_.stepCount);

    ImGui::Spacing();
    checkboxWithHint("Show fast-forward controls", &panel_.showAdvancedStepping,
        "Show target-step input to jump to an absolute step index.");
    if (panel_.showAdvancedStepping) {
        int runUntil = static_cast<int>(
            std::min<std::uint64_t>(panel_.runUntilTarget,
                static_cast<std::uint64_t>(kImGuiIntSafeMax)));
        if (sliderIntWithHint("Target step index", &runUntil, 0, kImGuiIntSafeMax,
            "Simulate until this absolute step index is reached.\nCurrent step + steps to run = target.")) {
            panel_.runUntilTarget = static_cast<std::uint64_t>(runUntil);
        }
        if (viz_.hasCachedCheckpoint) {
            const std::uint64_t cur = viz_.cachedCheckpoint.stateSnapshot.header.stepIndex;
            if (static_cast<std::uint64_t>(runUntil) > cur)
                ImGui::TextDisabled("Will run %llu more step(s)", (unsigned long long)(panel_.runUntilTarget - cur));
            else
                ImGui::TextColored(ImVec4(1.0f,0.5f,0.5f,1.0f), "Target is behind current step.");
        }
        if (PrimaryButton("Fast-Forward", ImVec2(-1.0f, 28.0f))) {
            cancelPendingSimulationSteps();
            std::string msg;
            runtime_.runUntil(panel_.runUntilTarget, msg);
            appendUserFacingOperationMessage(msg, "Fast-forward requested", "Validate target step arrival and inspect state summary.");
            requestSnapshotRefresh();
        }
    }
}

// Draws time control slider.
void drawTimeControlSection() {
    if (!runtime_.isRunning()) {
        panel_.playbackSpeedDirty = false;
        ImGui::TextDisabled("Start the simulation to use time controls.");
        return;
    }

    if (!panel_.playbackSpeedDirty) {
        panel_.playbackSpeed = runtime_.playbackSpeed();
    }

    if (sliderFloatWithHint("Playback speed", &panel_.playbackSpeed, 0.1f, 8.0f, "%.2fx",
        "Logical playback multiplier used by time control tools.")) {
        panel_.playbackSpeedDirty = true;
    }
    if (PrimaryButton("Apply speed", ImVec2(140.0f, 24.0f))) {
        std::string msg;
        if (runtime_.setPlaybackSpeed(panel_.playbackSpeed, msg)) {
            panel_.playbackSpeed = runtime_.playbackSpeed();
            panel_.playbackSpeedDirty = false;
        }
        appendUserFacingOperationMessage(
            msg,
            "Playback speed update requested",
            "Use time controls to verify desired pacing behavior.");
    }

    const std::uint64_t currentStep = viz_.hasCachedCheckpoint
        ? viz_.cachedCheckpoint.stateSnapshot.header.stepIndex
        : 0u;

    int seekTarget = static_cast<int>(std::min<std::uint64_t>(panel_.seekTargetStep, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
    if (sliderIntWithHint("Seek target step", &seekTarget, 0, kImGuiIntSafeMax,
        "Seek to absolute step. Backward seeks restore nearest checkpoint then replay deterministically.")) {
        panel_.seekTargetStep = static_cast<std::uint64_t>(seekTarget);
    }

    if (PrimaryButton("Seek", ImVec2(120.0f, 26.0f))) {
        seekToStepAndRefresh(panel_.seekTargetStep);
    }
    ImGui::SameLine();
    NumericSliderPairInt("Step backward", &panel_.backwardStepCount, 1, 100000, "%d", 55.0f);
    if (SecondaryButton("Apply backward step", ImVec2(-1.0f, 26.0f))) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.stepBackward(static_cast<std::uint32_t>(std::max(1, panel_.backwardStepCount)), msg);
        appendUserFacingOperationMessage(msg, "Backward step requested", "Inspect rollback result and continue timeline navigation.");
        requestSnapshotRefresh();
    }

    NumericSliderPairInt("Checkpoint interval", &panel_.checkpointIntervalSteps, 1, 100000, "%d", 55.0f);
    NumericSliderPairInt("Checkpoint retention", &panel_.checkpointRetentionCount, 1, 2000, "%d", 55.0f);
    if (PrimaryButton("Apply timeline policy", ImVec2(-1.0f, 24.0f))) {
        std::string msg;
        runtime_.configureCheckpointTimeline(
            static_cast<std::uint32_t>(std::max(1, panel_.checkpointIntervalSteps)),
            static_cast<std::size_t>(std::max(1, panel_.checkpointRetentionCount)),
            msg);
        appendUserFacingOperationMessage(msg, "Timeline checkpoint policy update requested", "Run simulation to populate checkpoints under new policy.");
    }

    const SimulationClockInfo clock = currentSimulationClock();
    const auto formatAbsoluteTime = [](const float value) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(3) << value << " s";
        if (std::isfinite(value)) {
            output << "  (" << std::setprecision(2)
                   << (value / 60.0f) << " min, "
                   << (value / 3600.0f) << " h)";
        }
        return output.str();
    };

    ImGui::TextDisabled("Absolute time: %s", formatAbsoluteTime(clock.value).c_str());
    ImGui::SameLine();
    ImGui::TextColored(
        clock.fromField ? ImVec4(0.50f, 0.85f, 0.55f, 1.0f) : ImVec4(0.80f, 0.72f, 0.45f, 1.0f),
        "[%s]",
        clock.sourceLabel.c_str());

    TimeControlStatus status;
    status.currentStep = currentStep;
    status.targetStep = panel_.seekTargetStep;
    status.simulationTime = clock.value;
    status.playbackSpeed = runtime_.playbackSpeed();
    ImGui::TextDisabled("%s", formatTimeControlStatus(status).c_str());

    const float progress = timeControlProgress(status);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));

    std::vector<std::uint64_t> timelineSteps;
    std::string timelineMsg;
    if (runtime_.timelineCheckpointSteps(timelineSteps, timelineMsg)) {
        ImGui::Spacing();
        if (timelineSteps.empty()) {
            ImGui::TextDisabled("Timeline checkpoints: none captured yet.");
        } else {
            ImGui::TextDisabled(
                "Timeline checkpoints: %zu stored, range %llu -> %llu",
                timelineSteps.size(),
                static_cast<unsigned long long>(timelineSteps.front()),
                static_cast<unsigned long long>(timelineSteps.back()));
            ImGui::TextDisabled("Click a stored step to jump there immediately.");

            const std::size_t begin = timelineSteps.size() > 10u ? timelineSteps.size() - 10u : 0u;
            for (std::size_t index = timelineSteps.size(); index-- > begin;) {
                const std::uint64_t step = timelineSteps[index];
                const bool isCurrent = step == currentStep;
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(35, 100, 70, 220));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 125, 90, 235));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(28, 85, 60, 255));
                }

                const std::string label = std::to_string(step) + "##timeline_step";
                if (ImGui::Button(label.c_str(), ImVec2(72.0f, 22.0f))) {
                    seekToStepAndRefresh(step);
                }

                if (isCurrent) {
                    ImGui::PopStyleColor(3);
                }

                if (index > begin && ImGui::GetContentRegionAvail().x > 78.0f) {
                    ImGui::SameLine();
                }
            }
        }
    }
}

// Draws checkpoint save/load section.
void drawCheckpointSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to use checkpoints.");
        return;
    }

    ImGui::TextDisabled("Change scope");
    ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.55f, 1.0f), "Applies now");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Session only");
    ImGui::TextDisabled("In-memory checkpoints are immediate runtime tools and are cleared when runtime stops.");

    inputTextWithHint("Label##cp", panel_.checkpointLabel, sizeof(panel_.checkpointLabel),
        "Identifier used to save/restore this checkpoint.\nAny alphanumeric string is valid.");

    const float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (PrimaryButton("Store##cp", ImVec2(btnW, 26.0f))) {
        std::string msg;
        [[maybe_unused]] const bool created = runtime_.createCheckpoint(panel_.checkpointLabel, msg);
        appendUserFacingOperationMessage(msg, "Checkpoint store requested", "Use restore/list controls to manage saved state anchors.");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Save the current state under this label (in-memory only).");
    ImGui::SameLine(0,4);
    if (PrimaryButton("Restore##cp", ImVec2(btnW, 26.0f))) {
        cancelPendingSimulationSteps();
        std::string msg;
        [[maybe_unused]] const bool restored = runtime_.restoreCheckpoint(panel_.checkpointLabel, msg);
        appendUserFacingOperationMessage(msg, "Checkpoint restore requested", "Review restored state before applying new interventions.");
        requestSnapshotRefresh();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Roll back to the saved checkpoint with this label.\nAll unsaved progress after that point is discarded.");
    ImGui::SameLine(0,4);
    if (SecondaryButton("List##cp", ImVec2(-1.0f, 26.0f))) {
        std::string msg;
        [[maybe_unused]] const bool queried = runtime_.listCheckpoints(msg);
        appendUserFacingOperationMessage(msg, "Checkpoint list requested", "Select a checkpoint to inspect, restore, rename, or delete.");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("List all stored in-memory checkpoint labels.");

    std::vector<CheckpointInfo> checkpointRecords;
    std::string checkpointMsg;
    if (runtime_.checkpointRecords(checkpointRecords, checkpointMsg)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Checkpoint browser");
        if (checkpointRecords.empty()) {
            ImGui::TextDisabled("No in-memory checkpoints recorded yet.");
        } else {
            const std::string selectedLabel = panel_.checkpointSelectedLabel;
            ImGui::TextDisabled(
                "%zu checkpoint%s available. Select one to inspect, restore, rename, or delete.",
                checkpointRecords.size(),
                checkpointRecords.size() == 1u ? "" : "s");

            if (ImGui::BeginTable(
                "CheckpointBrowser",
                6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, 190.0f))) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed, 105.0f);
                ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableHeadersRow();

                auto selectCheckpoint = [&](const CheckpointInfo& record) {
                    std::snprintf(panel_.checkpointSelectedLabel, sizeof(panel_.checkpointSelectedLabel), "%s", record.label.c_str());
                    if (panel_.checkpointRenameTarget[0] == '\0' || std::string(panel_.checkpointRenameTarget) == selectedLabel) {
                        std::snprintf(panel_.checkpointRenameTarget, sizeof(panel_.checkpointRenameTarget), "%s", record.label.c_str());
                    }
                };

                for (const auto& record : checkpointRecords) {
                    const bool isSelected = (selectedLabel == record.label);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    if (isSelected) {
                        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "● ");
                        ImGui::SameLine(0.0f, 2.0f);
                    }
                    if (ImGui::Selectable(record.label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                        selectCheckpoint(record);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("Select checkpoint '%s' for restore, rename, or comparison.", record.label.c_str());
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(record.stepIndex));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(record.timestampTicks));

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%016llX", static_cast<unsigned long long>(record.stateHash));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", static_cast<unsigned long long>(record.payloadBytes));

                    ImGui::TableSetColumnIndex(5);
                    if (ImGui::SmallButton(("Load##" + record.label).c_str())) {
                        std::snprintf(panel_.checkpointSelectedLabel, sizeof(panel_.checkpointSelectedLabel), "%s", record.label.c_str());
                        std::snprintf(panel_.checkpointRenameTarget, sizeof(panel_.checkpointRenameTarget), "%s", record.label.c_str());
                        cancelPendingSimulationSteps();
                        std::string msg;
                        if (runtime_.restoreCheckpoint(record.label, msg)) {
                            requestSnapshotRefresh();
                        }
                        appendLog(msg);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Select##" + record.label).c_str())) {
                        selectCheckpoint(record);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Delete##" + record.label).c_str())) {
                        std::snprintf(panel_.checkpointDeleteLabel, sizeof(panel_.checkpointDeleteLabel), "%s", record.label.c_str());
                        showCheckpointDeleteConfirm_ = true;
                        ImGui::OpenPopup("Delete Checkpoint");
                    }
                }

                ImGui::EndTable();
            }

            if (panel_.checkpointSelectedLabel[0] != '\0') {
                const std::string selectedCheckpointLabel = panel_.checkpointSelectedLabel;
                const auto selectedIt = std::find_if(
                    checkpointRecords.begin(),
                    checkpointRecords.end(),
                    [&](const CheckpointInfo& record) { return record.label == selectedCheckpointLabel; });

                if (selectedIt != checkpointRecords.end()) {
                    if (panel_.checkpointRenameTarget[0] == '\0') {
                        std::snprintf(panel_.checkpointRenameTarget, sizeof(panel_.checkpointRenameTarget), "%s", selectedIt->label.c_str());
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Selected checkpoint: %s", selectedIt->label.c_str());
                    ImGui::TextDisabled(
                        "Step %llu | ticks %llu | hash 0x%016llX | %llu bytes",
                        static_cast<unsigned long long>(selectedIt->stepIndex),
                        static_cast<unsigned long long>(selectedIt->timestampTicks),
                        static_cast<unsigned long long>(selectedIt->stateHash),
                        static_cast<unsigned long long>(selectedIt->payloadBytes));

                    RuntimeCheckpoint currentCheckpoint{};
                    std::string currentMsg;
                    if (runtime_.captureCheckpoint(currentCheckpoint, currentMsg, false /* computeHash */)) {
                        const auto& currentSnapshot = currentCheckpoint.stateSnapshot;
                        ImGui::TextDisabled("Current runtime: step %llu | ticks %llu | hash 0x%016llX",
                            static_cast<unsigned long long>(currentSnapshot.header.stepIndex),
                            static_cast<unsigned long long>(currentSnapshot.header.timestampTicks),
                            static_cast<unsigned long long>(currentSnapshot.stateHash));
                        ImGui::TextDisabled(
                            "Compare: step delta %+lld | hash match %s | profile match %s | run match %s",
                            static_cast<long long>(currentSnapshot.header.stepIndex) - static_cast<long long>(selectedIt->stepIndex),
                            currentSnapshot.stateHash == selectedIt->stateHash ? "yes" : "no",
                            currentCheckpoint.profileFingerprint == selectedIt->profileFingerprint ? "yes" : "no",
                            currentCheckpoint.runSignature.identityHash() == selectedIt->runIdentityHash ? "yes" : "no");
                    } else {
                        ImGui::TextDisabled("Current runtime snapshot unavailable: %s", currentMsg.c_str());
                    }

                    inputTextWithHint("Rename selected##cp", panel_.checkpointRenameTarget, sizeof(panel_.checkpointRenameTarget),
                        "New label for the selected in-memory checkpoint.");
                    if (PrimaryButton("Rename selected checkpoint", ImVec2(220.0f, 24.0f))) {
                        std::string msg;
                        if (runtime_.renameCheckpoint(selectedIt->label, panel_.checkpointRenameTarget, msg)) {
                            std::snprintf(panel_.checkpointSelectedLabel, sizeof(panel_.checkpointSelectedLabel), "%s", panel_.checkpointRenameTarget);
                            appendLog(msg);
                        } else {
                            appendLog(msg);
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("Rename the selected checkpoint label in-memory.");
                    }
                }
            }
        }
    } else {
        ImGui::TextWrapped("Checkpoint browser unavailable: %s", checkpointMsg.c_str());
    }

    ImGui::Spacing();
    ImGui::TextDisabled("In-memory checkpoints are lost when the simulation stops.\nUse Save & Exit to persist to disk.");
}

// Draws confirmation modals for destructive actions.
void drawConfirmationModals() {
    if (showStopResetConfirm_) {
        if (ImGui::BeginPopupModal("Confirm Stop and Reset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool running = runtime_.isRunning();
            const bool hasWorld = !runtime_.activeWorldName().empty();

            std::size_t pendingImmediateWrites = 0;
            std::size_t queuedDeferredEvents = 0;
            std::size_t pendingScheduledPerturbations = 0;
            std::size_t runtimeManualEventCount = 0;
            std::string effectLedgerMessage;
            bool pendingRestart = false;
            if (running) {
                [[maybe_unused]] const bool ledgerRead = runtime_.effectLedgerCounts(
                    pendingImmediateWrites,
                    queuedDeferredEvents,
                    pendingScheduledPerturbations,
                    runtimeManualEventCount,
                    effectLedgerMessage);

                const auto& cfg = runtime_.config();
                const int runtimeTierIndex =
                    (cfg.tier == ModelTier::A) ? 0 :
                    (cfg.tier == ModelTier::B) ? 1 : 2;
                const std::string runtimeTemporal = app::temporalPolicyToString(cfg.temporalPolicy);
                const int runtimeTemporalIndex =
                    (runtimeTemporal == "uniform") ? 0 :
                    (runtimeTemporal == "phased") ? 1 : 2;
                pendingRestart =
                    runtimeTierIndex != panel_.tierIndex ||
                    runtimeTemporalIndex != panel_.temporalIndex;
            }

            const bool hasUnsavedRuntimeChanges = running && (
                pendingImmediateWrites > 0 ||
                queuedDeferredEvents > 0 ||
                pendingScheduledPerturbations > 0 ||
                runtimeManualEventCount > 0 ||
                pendingRestart);

            ImGui::TextWrapped("Stop the runtime and reset the active simulation state?");
            if (hasUnsavedRuntimeChanges) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f), "Unsaved runtime effects detected:");
                if (pendingImmediateWrites > 0) {
                    ImGui::BulletText("pending immediate writes: %zu patch%s", pendingImmediateWrites, pendingImmediateWrites == 1u ? "" : "es");
                }
                if (queuedDeferredEvents > 0 || pendingScheduledPerturbations > 0) {
                    ImGui::BulletText(
                        "queued deferred events: %zu queue item%s, %zu scheduled perturbation%s",
                        queuedDeferredEvents,
                        queuedDeferredEvents == 1u ? "" : "s",
                        pendingScheduledPerturbations,
                        pendingScheduledPerturbations == 1u ? "" : "s");
                }
                if (runtimeManualEventCount > 0) {
                    ImGui::BulletText("unsaved runtime edits in event log: %zu", runtimeManualEventCount);
                }
                if (pendingRestart) {
                    ImGui::BulletText("restart-required profile/temporal changes are staged");
                }
                ImGui::TextDisabled("Choose an explicit commit/revert action before destructive stop.");
            } else {
                ImGui::TextDisabled("No unsaved runtime effects detected.");
            }

            const auto performStopReset = [&]() {
                viz_.autoRun = false;
                cancelPendingSimulationSteps();
                if (!runtime_.activeWorldName().empty()) {
                    saveDisplayPrefs();
                }
                std::string msg;
                runtime_.stop(msg);
                appendUserFacingOperationMessage(msg, "Runtime stop and reset executed", "Restart simulation or return to model/world selection.");
                requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Pause);
                showStopResetConfirm_ = false;
                ImGui::CloseCurrentPopup();
            };

            ImGui::Spacing();
            if (hasUnsavedRuntimeChanges && hasWorld) {
                if (PrimaryButton("Save world, then stop", ImVec2(180.0f, 28.0f))) {
                    saveDisplayPrefs();
                    std::string saveMsg;
                    const bool saved = runtime_.saveActiveWorld(saveMsg);
                    appendUserFacingOperationMessage(saveMsg, "Save world before destructive stop", "After save completes, runtime will stop/reset.");
                    if (saved) {
                        performStopReset();
                    }
                }
                ImGui::SameLine();
                if (SecondaryButton("Stop without saving", ImVec2(170.0f, 28.0f))) {
                    performStopReset();
                }
                ImGui::SameLine();
                if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                    showStopResetConfirm_ = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                if (PrimaryButton("Stop and Reset", ImVec2(160.0f, 28.0f))) {
                    performStopReset();
                }
                ImGui::SameLine();
                if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                    showStopResetConfirm_ = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    if (showCheckpointDeleteConfirm_) {
        if (ImGui::BeginPopupModal("Delete Checkpoint", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete checkpoint '%s'? This cannot be undone.",
                panel_.checkpointDeleteLabel[0] != '\0' ? panel_.checkpointDeleteLabel : "<unknown>");
            ImGui::Spacing();
            if (PrimaryButton("Delete", ImVec2(130.0f, 28.0f))) {
                std::string msg;
                if (runtime_.deleteCheckpoint(panel_.checkpointDeleteLabel, msg)) {
                    if (std::string(panel_.checkpointSelectedLabel) == panel_.checkpointDeleteLabel) {
                        panel_.checkpointSelectedLabel[0] = '\0';
                        panel_.checkpointRenameTarget[0] = '\0';
                    }
                    panel_.checkpointDeleteLabel[0] = '\0';
                    appendLog(msg);
                } else {
                    appendLog(msg);
                }
                showCheckpointDeleteConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                panel_.checkpointDeleteLabel[0] = '\0';
                showCheckpointDeleteConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    if (showWizardResetConfirm_) {
        if (ImGui::BeginPopupModal("Reset Wizard Parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Reset the wizard parameters back to the recommended defaults? Unsaved wizard edits will be lost.");
            ImGui::Spacing();
            if (PrimaryButton("Reset parameters", ImVec2(150.0f, 28.0f))) {
                syncPanelFromConfig();
                const auto recommended = GenerationAdvisor::recommendGenerationMode(sessionUi_.selectedModelCatalog, {});
                const InitialConditionType refined = fallbackRuntimeSupportedMode(
                    refineRecommendedModeForKnownModels(sessionUi_.selectedModelCatalog, recommended.recommendedType));
                applyGenerationDefaultsForMode(panel_, sessionUi_.selectedModelCatalog, refined, true);
                applyAutoVariableBindingsForMode(panel_, sessionUi_.selectedModelCellStateVariables, refined);
                viz_.generationPreviewDisplayType = recommendedPreviewDisplayTypeForMode(refined);
                sessionUi_.generationPreviewSourceIndex = recommendedPreviewSourceForMode(refined);
                sessionUi_.generationPreviewChannelIndex = findPreferredVariableIndex(
                    sessionUi_.selectedModelCatalog,
                    sessionUi_.selectedModelCellStateVariables,
                    {"fire_state", "living", "water", "state", "concentration", "temperature", "vegetation", "velocity", "oxygen"},
                    0);
                sessionUi_.generationModeIndex = static_cast<int>(refined);
                rebuildVariableInitializationSettings(sessionUi_, sessionUi_.selectedModelCatalog);
                panel_.useManualSeed = false;
                panel_.seed = generateRandomSeed();
                const std::string baseHint = sessionUi_.selectedModelName[0] != '\0'
                    ? std::string(sessionUi_.selectedModelName)
                    : std::string("world");
                std::string suggestedName = runtime_.suggestWorldNameFromHint(baseHint);
                std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", suggestedName.c_str());
                sessionUi_.wizardStepIndex = 0;
                deferredWizardInitialization_ = DeferredWizardInitialization{};
                showWizardResetConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                showWizardResetConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

// Draws guardrails section for numeric constraints.
void drawGuardrailsSection() {
    ImGui::TextDisabled("Numeric guardrail tuning is controlled by the active profile and runtime execution mode.");
    ImGui::Spacing();
    ImGui::TextWrapped("Use profile configuration to set stability and guardrail behavior. The current GUI build does not expose direct guardrail fields.");
    ImGui::Spacing();
    if (SecondaryButton("Log runtime status", ImVec2(-1.0f, 26.0f))) {
        std::string msg;
        runtime_.status(msg);
        appendLog(msg);
    }
}

//
// Tab: Display
//
// Draws display tab with visualization settings.
void drawDisplayTab() {
    ImGui::BeginChild("DispTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    PushSectionTint(6);
    if (ImGui::CollapsingHeader("Per-View Display Settings", ImGuiTreeNodeFlags_DefaultOpen))
        drawViewportSettingsSection();
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(7);
    if (ImGui::CollapsingHeader("Overlay & Domain (Per View)"))
        drawOverlaySection();
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(8);
    if (ImGui::CollapsingHeader("Camera & Optics (Per View)"))
        drawOpticsSection();
    PopSectionTint();

    ImGui::EndChild();
}

// Draws viewport settings section.
void drawViewportSettingsSection() {
    ensureViewportStateConsistency();

    if (viz_.fieldNames.empty()) {
        ImGui::TextDisabled("No fields available. Start the simulation or refresh.");
        return;
    }
    clampVisualizationIndices();

    ImGui::TextDisabled("Open views: %d", static_cast<int>(viz_.viewports.size()));
    ImGui::SameLine();
    if (SecondaryButton("Add view", ImVec2(110.0f, 24.0f))) {
        addViewportConfig();
    }
    ImGui::SameLine();
    const bool canCloseActive = viz_.viewports.size() > 1u;
    if (!canCloseActive) {
        ImGui::BeginDisabled();
    }
    if (SecondaryButton("Close active", ImVec2(110.0f, 24.0f))) {
        removeViewportConfig(static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, static_cast<int>(viz_.viewports.size()) - 1)));
    }
    if (!canCloseActive) {
        ImGui::EndDisabled();
    }

    viz_.activeViewportEditor = std::clamp(viz_.activeViewportEditor, 0, static_cast<int>(viz_.viewports.size()) - 1);

    std::vector<std::size_t> closeRequests;
    if (ImGui::BeginTabBar("ViewportTabs")) {
        for (std::size_t i = 0; i < viz_.viewports.size(); ++i) {
            std::string label = "View " + std::to_string(i + 1u);
            if (i < 11u) {
                label += " [F" + std::to_string(i + 2u) + "]";
            }
            bool tabOpen = true;
            const ImGuiTabItemFlags tabFlags = (viewportTabSelectionRequest_ == static_cast<int>(i))
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label.c_str(), &tabOpen, tabFlags)) {
                viz_.activeViewportEditor = static_cast<int>(i);
                drawSingleViewportEditor(viz_.viewports[i], static_cast<int>(i));
                ImGui::EndTabItem();
            }
            if (!tabOpen && viz_.viewports.size() > 1u) {
                closeRequests.push_back(i);
            }
        }

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
            addViewportConfig();
        }

        ImGui::EndTabBar();
    }

    viewportTabSelectionRequest_ = -1;

    for (auto it = closeRequests.rbegin(); it != closeRequests.rend(); ++it) {
        removeViewportConfig(*it);
    }
}

// Draws single viewport editor.
// @param vp Viewport configuration to edit
// @param viewportIndex Viewport index
void drawSingleViewportEditor(ViewportConfig& vp, const int viewportIndex) {
    static constexpr const char* dispTypeNames[] = {
        "Scalar Field", "Surface Category", "Relative Elevation", "Surface Water", "Moisture Map", "Wind Field"};
    static constexpr const char* colorMapNames[] = {
        "Turbo", "Grayscale", "Diverging", "Water"};
    static constexpr const char* normNames[] = {
        "Per-frame auto", "Sticky per-field", "Fixed manual"};
    static constexpr const char* renderModeNames[] = {
        "Heatmap", "Vector", "Contour", "Custom rule"};
    static constexpr const char* heatmapNormNames[] = {
        "Linear", "Logarithmic", "Sqrt", "Power", "Quantile"};
    static constexpr const char* heatmapPaletteNames[] = {
        "Viridis", "Hot", "Cool", "Jet", "Turbo", "Custom"};

    ImGui::TextDisabled("Render mode");
    int renderMode = static_cast<int>(vp.renderMode);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##rm", &renderMode, renderModeNames,
            static_cast<int>(std::size(renderModeNames)))) {
        vp.renderMode = static_cast<ViewportRenderMode>(std::clamp(renderMode, 0, static_cast<int>(std::size(renderModeNames) - 1)));
        vp.showVectorField = (vp.renderMode == ViewportRenderMode::Vector);
        vp.showContours = (vp.renderMode == ViewportRenderMode::Contour);
        vp.customRuleEnabled = (vp.renderMode == ViewportRenderMode::CustomRule);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Select viewport rendering mode: heatmap, vector, contour, or custom rule blend.");
    }

    // Primary field
    ImGui::TextDisabled("Primary field");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##primfield",
            viz_.fieldNames.at(
                static_cast<std::size_t>(vp.primaryFieldIndex)).c_str())) {
        for (int i = 0; i < (int)viz_.fieldNames.size(); ++i) {
            if (ImGui::Selectable(viz_.fieldNames[i].c_str(),
                    vp.primaryFieldIndex == i))
                vp.primaryFieldIndex = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Primary scalar field displayed in this viewport.");

    // Display type
    ImGui::TextDisabled("Display type");
    int dt = static_cast<int>(vp.displayType);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##dt", &dt, dispTypeNames,
            static_cast<int>(std::size(dispTypeNames))))
        vp.displayType = static_cast<DisplayType>(dt);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(
            "Scalar Field - raw normalized values.\n"
            "Surface Category - deep/shallow water, wet shoreline, and landform bands.\n"
            "Relative Elevation - depth-banded elevation relative to water level.\n"
            "Surface Water - water depth visualization.\n"
            "Moisture Map - blended humidity and surface wetness.\n"
            "Wind Field - automatic vector-tagged axis field background and arrows.");

    checkboxWithHint("Sparse overlay entries##vp", &vp.showSparseOverlay,
        "Include sparse overlay values (event/input patches) in this view.");

    ImGui::Separator();
    ImGui::TextDisabled("Domain classification (this view)");
    checkboxWithHint("Auto water level##vp", &vp.displayManager.autoWaterLevel,
        "Derive water/land threshold from terrain elevation distribution for this view.");
    if (vp.displayManager.autoWaterLevel) {
        sliderFloatWithHint("Auto quantile##vp", &vp.displayManager.autoWaterQuantile,
            0.05f, 0.95f, "%.3f",
            "Terrain percentile used to place waterline for this view.");
    } else {
        sliderFloatWithHint("Manual water level##vp", &vp.displayManager.waterLevel,
            0.0f, 1.0f, "%.3f",
            "Fixed elevation threshold separating water from land for this view.");
    }
    sliderFloatWithHint("Lowland breakpoint##vp", &vp.displayManager.lowlandThreshold,
        0.0f, 1.0f, "%.3f",
        "Elevation below which land is classified as lowland/beach.");
    vp.displayManager.highlandThreshold = std::max(
        vp.displayManager.highlandThreshold,
        vp.displayManager.lowlandThreshold + 0.01f);
    sliderFloatWithHint("Highland breakpoint##vp", &vp.displayManager.highlandThreshold,
        0.0f, 1.0f, "%.3f",
        "Elevation above which land is classified as highland/mountain.");
    sliderFloatWithHint("Shallow water depth##vp", &vp.displayManager.shallowWaterDepth,
        0.0f, 0.20f, "%.3f",
        "Depth threshold for shallow coastal water versus deeper ocean.");
    sliderFloatWithHint("Surface wetness threshold##vp", &vp.displayManager.waterPresenceThreshold,
        0.0f, 0.5f, "%.3f",
        "Normalized surface-water threshold for wet shoreline classification.");
    sliderFloatWithHint("High humidity threshold##vp", &vp.displayManager.highMoistureThreshold,
        0.0f, 1.0f, "%.3f",
        "Humidity threshold used by moisture/surface-category modes.");

    // Color map
    const bool windFieldMode = (vp.displayType == DisplayType::WindField);
    if (!windFieldMode) {
        ImGui::TextDisabled("Palette");
        int cm = static_cast<int>(vp.colorMapMode);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##cm", &cm, colorMapNames,
                static_cast<int>(std::size(colorMapNames))))
            vp.colorMapMode = static_cast<ColorMapMode>(cm);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Turbo - rainbow heatmap, good for scalar fields.\n"
                "Grayscale - luminance-only, perception-accurate.\n"
                "Diverging - red-grey-blue for centered data.\n"
                "Water - blue depth gradient, good for hydrology.");
    } else {
        ImGui::TextDisabled("Wind Field uses a dedicated calm-to-storm palette.");
    }

    // Normalization
    if (!windFieldMode) {
        ImGui::TextDisabled("Range");
        int nm = static_cast<int>(vp.normalizationMode);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##nm", &nm, normNames,
                static_cast<int>(std::size(normNames))))
            vp.normalizationMode = static_cast<NormalizationMode>(nm);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(
                "Per-frame auto - recomputes min/max every frame.\n"
                "Sticky per-field - range only expands, never contracts (smoother).\n"
                "Fixed manual - you specify exact min/max below.");
        if (vp.normalizationMode == NormalizationMode::StickyPerField) {
            if (SecondaryButton("Reset sticky##rst", ImVec2(130.0f, 22.0f)))
                vp.stickyRanges.clear();
            ImGui::SameLine();
            checkboxWithHint("Show range HUD", &vp.showRangeDetails,
                "Show current min/max/normalization mode on the viewport.");
        } else if (vp.normalizationMode == NormalizationMode::FixedManual) {
            NumericSliderPair("Min##fr", &vp.fixedRangeMin, -500.0f, 500.0f, "%.3f", 70.0f);
            NumericSliderPair("Max##fr", &vp.fixedRangeMax, -500.0f, 500.0f, "%.3f", 70.0f);
        }
    } else {
        ImGui::TextDisabled("Wind Field normalizes from the fixed wind vector range.");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Heatmap pipeline");
    ImGui::TextDisabled("Normalization");
    int hNorm = static_cast<int>(vp.heatmapNormalization);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##hmnorm", &hNorm, heatmapNormNames,
            static_cast<int>(std::size(heatmapNormNames)))) {
        vp.heatmapNormalization = static_cast<HeatmapNormalization>(std::clamp(hNorm, 0, static_cast<int>(std::size(heatmapNormNames) - 1)));
    }
    ImGui::TextDisabled("Palette");
    int hPalette = static_cast<int>(vp.heatmapColorMap);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##hmpalette", &hPalette, heatmapPaletteNames,
            static_cast<int>(std::size(heatmapPaletteNames)))) {
        vp.heatmapColorMap = static_cast<HeatmapColorMap>(std::clamp(hPalette, 0, static_cast<int>(std::size(heatmapPaletteNames) - 1)));
    }
    if (vp.heatmapNormalization == HeatmapNormalization::Power) {
        NumericSliderPair("Power exponent##hmpow", &vp.heatmapPowerExponent, 0.1f, 8.0f, "%.2f", 70.0f);
    }
    if (vp.heatmapNormalization == HeatmapNormalization::Quantile) {
        NumericSliderPair("Quantile low##hmql", &vp.heatmapQuantileLow, 0.0f, 0.95f, "%.2f", 70.0f);
        NumericSliderPair("Quantile high##hmqh", &vp.heatmapQuantileHigh, 0.05f, 1.0f, "%.2f", 70.0f);
        vp.heatmapQuantileHigh = std::max(vp.heatmapQuantileHigh, vp.heatmapQuantileLow + 0.01f);
    }

    if (vp.renderMode == ViewportRenderMode::Contour) {
        checkboxWithHint("Show contour overlay", &vp.showContours,
            "Render marching-squares contour lines over the active scalar field.");
        if (vp.showContours) {
            NumericSliderPair("Contour interval##cti", &vp.contourInterval, 0.001f, 5.0f, "%.3f", 70.0f);
            NumericSliderPairInt("Max contour levels##ctl", &vp.contourMaxLevels, 2, 128, "%d", 55.0f);
        }
    }

    if (vp.renderMode == ViewportRenderMode::CustomRule) {
        checkboxWithHint("Enable custom rules", &vp.customRuleEnabled,
            "Applies ordered rule blending over the heatmap output.");
        if (vp.customRuleEnabled) {
            const int maxViewportIndex = std::max(0, static_cast<int>(viewportRenderRules_.size()) - 1);
            const std::size_t rulesIndex = static_cast<std::size_t>(std::clamp(viewportIndex, 0, maxViewportIndex));
            if (SecondaryButton("Save rule preset", ImVec2(130.0f, 24.0f))) {
                std::filesystem::create_directories(std::filesystem::path("profiles") / "render_presets");
                RenderPreset preset;
                preset.name = "viewport_" + std::to_string(viewportIndex + 1);
                preset.rules = viewportRenderRules_[rulesIndex];
                std::string msg;
                const auto path = (std::filesystem::path("profiles") / "render_presets" / (preset.name + ".wsrender")).string();
                if (saveRenderPreset(preset, path, msg)) appendLog(msg);
                else appendLog(msg);
            }
            ImGui::SameLine();
            if (SecondaryButton("Load rule preset", ImVec2(130.0f, 24.0f))) {
                RenderPreset preset;
                std::string msg;
                const auto path = (std::filesystem::path("profiles") / "render_presets" /
                    ("viewport_" + std::to_string(viewportIndex + 1) + ".wsrender")).string();
                if (loadRenderPreset(path, preset, msg)) {
                    viewportRenderRules_[rulesIndex] = std::move(preset.rules);
                    appendLog(msg);
                } else {
                    appendLog(msg);
                }
            }
            ImGui::TextDisabled("Active rules: %d", static_cast<int>(viewportRenderRules_[rulesIndex].size()));
        }
    }

    checkboxWithHint("Show legend", &vp.showLegend,
        "Overlay min/max/field name on the viewport.");

    ImGui::Separator();
    ImGui::TextDisabled("Viewport camera");
    const int maxViewportIndex = std::max(0, static_cast<int>(viz_.viewports.size()) - 1);
    const std::size_t clampedViewportIndex = static_cast<std::size_t>(std::clamp(viewportIndex, 0, maxViewportIndex));
    const auto& cam = viewportManager_.camera(clampedViewportIndex);
    float zoom = cam.zoom;
    float panX = cam.panX;
    float panY = cam.panY;
    if (NumericSliderPair("Zoom##vpzoom", &zoom, 0.05f, 12.0f, "%.2f", 70.0f)) {
        viewportManager_.setZoom(clampedViewportIndex, zoom);
    }
    if (NumericSliderPair("Pan X##vppx", &panX, -4000.0f, 4000.0f, "%.0f", 70.0f)) {
        viewportManager_.setPan(clampedViewportIndex, panX, panY);
    }
    if (NumericSliderPair("Pan Y##vppy", &panY, -4000.0f, 4000.0f, "%.0f", 70.0f)) {
        viewportManager_.setPan(clampedViewportIndex, panX, panY);
    }
    if (SecondaryButton("Fit viewport", ImVec2(120.0f, 22.0f))) {
        viewportManager_.fit(clampedViewportIndex);
    }
    ImGui::SameLine();
    if (SecondaryButton("Screenshot", ImVec2(120.0f, 22.0f))) {
        const auto nowMs = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        std::filesystem::create_directories(std::filesystem::path("checkpoints") / "screenshots");
        const auto out = (std::filesystem::path("checkpoints") / "screenshots" /
            ("viewport_" + std::to_string(viewportIndex + 1) + "_" + std::to_string(nowMs) + ".ppm")).string();
        viewportManager_.requestScreenshot(clampedViewportIndex, out);
    }
    bool syncPan = viewportManager_.syncPan();
    if (checkboxWithHint("Sync pan across viewports", &syncPan,
        "When enabled, panning one viewport updates all active viewports.")) {
        viewportManager_.setSyncPan(syncPan);
    }
    bool syncZoom = viewportManager_.syncZoom();
    if (checkboxWithHint("Sync zoom across viewports", &syncZoom,
        "When enabled, zoom updates are broadcast to all viewports.")) {
        viewportManager_.setSyncZoom(syncZoom);
    }

    // Vector overlay
    ImGui::Separator();
    if (windFieldMode) {
        ImGui::Indent();
        checkboxWithHint("Show magnitude background", &vp.showWindMagnitudeBackground,
            "Render wind speed magnitude behind the arrows.\n"
            "Disable for a cleaner vector-only view.");
        NumericSliderPairInt("Arrow density##wind", &vp.vectorStride, 1, 64, "%d", 55.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Sample one arrow every N cells. Higher = fewer, larger arrows.");
        NumericSliderPair("Arrow scale##wind", &vp.vectorScale, 0.02f, 3.0f, "%.2f", 70.0f);
        ImGui::TextDisabled("Uses metadata-tagged vector X/Y fields automatically.");
        ImGui::Unindent();
    } else {
        checkboxWithHint("Vector field overlay", &vp.showVectorField,
            "Draw arrows from two scalar fields to indicate direction/magnitude.\n"
            "Useful for any vector-tagged transport/flow axes or gradients.");
        if (vp.showVectorField) {
            ImGui::Indent();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("X-axis field##vx",
                    viz_.fieldNames.at(
                        static_cast<std::size_t>(vp.vectorXFieldIndex)).c_str())) {
                for (int i = 0; i < (int)viz_.fieldNames.size(); ++i)
                    if (ImGui::Selectable(viz_.fieldNames[i].c_str(), vp.vectorXFieldIndex==i))
                        vp.vectorXFieldIndex = i;
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Y-axis field##vy",
                    viz_.fieldNames.at(
                        static_cast<std::size_t>(vp.vectorYFieldIndex)).c_str())) {
                for (int i = 0; i < (int)viz_.fieldNames.size(); ++i)
                    if (ImGui::Selectable(viz_.fieldNames[i].c_str(), vp.vectorYFieldIndex==i))
                        vp.vectorYFieldIndex = i;
                ImGui::EndCombo();
            }
            NumericSliderPairInt("Arrow stride##vs", &vp.vectorStride, 1, 64, "%d", 55.0f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Sample one arrow every N cells. Higher = fewer, larger arrows.");
            NumericSliderPair("Arrow scale##vsc", &vp.vectorScale, 0.02f, 3.0f, "%.2f", 70.0f);
            ImGui::Unindent();
        }
    }
}

// Draws overlay section for vector/contour display.
void drawOverlaySection() {
    ensureViewportStateConsistency();
    const int maxViewportIndex = std::max(0, static_cast<int>(viz_.viewports.size()) - 1);
    const std::size_t idx = static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, maxViewportIndex));
    auto& vp = viz_.viewports[idx];

    ImGui::TextDisabled("Editing overlay controls for View %d", static_cast<int>(idx + 1u));

    checkboxWithHint("Domain boundary", &vp.showBoundary,
        "Draw a border rectangle around the simulation domain for this view.");
    if (vp.showBoundary) {
        ImGui::Indent();
        sliderFloatWithHint("Opacity##bo", &vp.boundaryOpacity, 0.0f, 1.0f, "%.2f",
            "Boundary line alpha.");
        sliderFloatWithHint("Thickness##bt", &vp.boundaryThickness, 0.5f, 6.0f, "%.1f",
            "Boundary line width in pixels.");
        ImGui::Unindent();
    }

    checkboxWithHint("Cell grid lines", &vp.showGrid,
        "Draw lines between cells in this view only. Visible when cells are large enough.");
    if (vp.showGrid) {
        ImGui::Indent();
        sliderFloatWithHint("Grid opacity##go", &vp.gridOpacity, 0.0f, 1.0f, "%.2f",
            "Grid line transparency.");
        sliderFloatWithHint("Grid thickness##gt", &vp.gridLineThickness, 0.5f, 3.0f, "%.2f",
            "Grid line width in pixels.");
        ImGui::Unindent();
    }

    checkboxWithHint("Sparse overlay entries", &vp.showSparseOverlay,
        "Include sparse overlay values (event/input patches) in this view.");

    if (SecondaryButton("Apply overlay settings to all views", ImVec2(-1.0f, 24.0f))) {
        for (auto& view : viz_.viewports) {
            view.showBoundary = vp.showBoundary;
            view.boundaryOpacity = vp.boundaryOpacity;
            view.boundaryThickness = vp.boundaryThickness;
            view.showGrid = vp.showGrid;
            view.gridOpacity = vp.gridOpacity;
            view.gridLineThickness = vp.gridLineThickness;
            view.showSparseOverlay = vp.showSparseOverlay;
        }
    }
}

// Draws optics section for color mapping.
void drawOpticsSection() {
    ensureViewportStateConsistency();
    const int maxViewportIndex = std::max(0, static_cast<int>(viz_.viewports.size()) - 1);
    const std::size_t idx = static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, maxViewportIndex));
    auto& vp = viz_.viewports[idx];
    auto cam = viewportManager_.camera(idx);
    if (sliderFloatWithHint("Zoom", &cam.zoom, 0.05f, 12.0f, "%.2f",
        "Viewport zoom. Shortcuts: +/- keys.  R to reset.")) {
        viewportManager_.setZoom(idx, cam.zoom);
    }
    if (sliderFloatWithHint("Pan X", &cam.panX, -4000.0f, 4000.0f, "%.0f",
        "Horizontal pan offset in pixels.")) {
        viewportManager_.setPan(idx, cam.panX, cam.panY);
    }
    if (sliderFloatWithHint("Pan Y", &cam.panY, -4000.0f, 4000.0f, "%.0f",
        "Vertical pan offset in pixels.")) {
        viewportManager_.setPan(idx, cam.panX, cam.panY);
    }
    if (SecondaryButton("Reset camera  [R]", ImVec2(-1.0f, 22.0f))) {
        viewportManager_.fit(idx);
    }
    ImGui::Separator();
    sliderFloatWithHint("Brightness", &vp.brightness, 0.1f, 3.0f, "%.2f",
        "Post-colormap brightness multiplier.");
    sliderFloatWithHint("Contrast",   &vp.contrast,   0.1f, 3.0f, "%.2f",
        "Post-colormap contrast around mid-gray.");
    sliderFloatWithHint("Gamma",      &vp.gamma,       0.2f, 3.0f, "%.2f",
        "Display gamma correction applied after contrast/brightness.");
    checkboxWithHint("Invert colors", &vp.invertColors,
        "Invert the color mapping (useful for white-background screenshots).");

    if (SecondaryButton("Apply camera/optics to all views", ImVec2(-1.0f, 24.0f))) {
        const auto referenceCam = viewportManager_.camera(idx);
        for (std::size_t i = 0; i < viz_.viewports.size(); ++i) {
            auto& view = viz_.viewports[i];
            view.brightness = vp.brightness;
            view.contrast = vp.contrast;
            view.gamma = vp.gamma;
            view.invertColors = vp.invertColors;
            viewportManager_.setZoom(i, referenceCam.zoom);
            viewportManager_.setPan(i, referenceCam.panX, referenceCam.panY);
        }
    }
}

// Tab: Analysis
// Draws analysis tab with statistics.
void drawAnalysisTab() {
    ImGui::BeginChild("AnalysisTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    PushSectionTint(7);
    if (ImGui::CollapsingHeader("Guided analysis recipes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Start with a question, then run one recipe to configure probes and comparisons quickly.");

        static int questionLauncherIndex = 0;
        static constexpr std::array<const char*, 4> kQuestionLauncherItems = {
            "Where did instability begin?",
            "What changed after perturbation X?",
            "Is this run still conserving target quantity?",
            "How different is this from checkpoint Y?"
        };

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(
            "Question launcher",
            &questionLauncherIndex,
            kQuestionLauncherItems.data(),
            static_cast<int>(kQuestionLauncherItems.size()));

        const auto ensureSummaryField = [&]() -> std::string {
            if (panel_.summaryVariable[0] != '\0') {
                return std::string(panel_.summaryVariable);
            }
            if (viz_.fieldNames.empty()) {
                return {};
            }
            std::snprintf(panel_.summaryVariable, sizeof(panel_.summaryVariable), "%s", viz_.fieldNames.front().c_str());
            return viz_.fieldNames.front();
        };

        const auto focusActiveViewportOnField = [&](const std::string& fieldName) {
            if (fieldName.empty()) {
                return;
            }
            const auto fieldIt = std::find(viz_.fieldNames.begin(), viz_.fieldNames.end(), fieldName);
            if (fieldIt == viz_.fieldNames.end() || viz_.viewports.empty()) {
                return;
            }
            const int fieldIndex = static_cast<int>(std::distance(viz_.fieldNames.begin(), fieldIt));
            const int activeViewportIndex = std::clamp(viz_.activeViewportEditor, 0, static_cast<int>(viz_.viewports.size()) - 1);
            viz_.viewports[static_cast<std::size_t>(activeViewportIndex)].primaryFieldIndex = fieldIndex;
        };

        if (PrimaryButton("Launch selected question workflow", ImVec2(-1.0f, 24.0f))) {
            switch (questionLauncherIndex) {
                case 0: {
                    StepDiagnostics diagnostics;
                    std::string diagnosticsMsg;
                    if (runtime_.lastStepDiagnostics(diagnostics, diagnosticsMsg)) {
                        const std::string primaryField = ensureSummaryField();
                        if (!primaryField.empty()) {
                            ProbeDefinition definition;
                            definition.id = primaryField + "_instability_watch";
                            definition.kind = ProbeKind::GlobalScalar;
                            definition.variableName = primaryField;
                            std::string addMsg;
                            runtime_.addProbe(definition, addMsg);
                            appendLog(addMsg);
                            focusActiveViewportOnField(primaryField);
                        }

                        const std::string firstViolation = diagnostics.constraintViolations.empty()
                            ? std::string("none_detected")
                            : diagnostics.constraintViolations.front();
                        appendLog(
                            "question_launcher_result question=instability_begin "
                            "constraint_violations=" + std::to_string(diagnostics.constraintViolations.size()) +
                            " stability_alerts=" + std::to_string(diagnostics.stabilityAlerts.size()) +
                            " first_violation=" + firstViolation);
                    } else {
                        appendLog(diagnosticsMsg);
                    }
                    break;
                }
                case 1: {
                    std::vector<ManualEventRecord> events;
                    std::string eventsMsg;
                    if (runtime_.manualEventLog(events, eventsMsg)) {
                        auto it = std::find_if(events.rbegin(), events.rend(), [](const ManualEventRecord& event) {
                            return event.kind == ManualEventKind::Perturbation;
                        });

                        if (it != events.rend()) {
                            const auto& event = *it;
                            panel_.seekTargetStep = event.step;
                            focusActiveViewportOnField(event.variable);
                            if (!event.variable.empty()) {
                                ProbeDefinition definition;
                                definition.id = event.variable + "_post_perturb_trend";
                                definition.kind = ProbeKind::GlobalScalar;
                                definition.variableName = event.variable;
                                std::string addMsg;
                                runtime_.addProbe(definition, addMsg);
                                appendLog(addMsg);
                            }

                            appendLog(
                                "question_launcher_result question=post_perturbation "
                                "variable=" + event.variable +
                                " perturb_step=" + std::to_string(event.step) +
                                " action=seek_target_prepared");
                        } else {
                            appendLog("question_launcher_blocked question=post_perturbation reason=no_perturbation_events");
                        }
                    } else {
                        appendLog(eventsMsg);
                    }
                    break;
                }
                case 2: {
                    const std::string targetField = ensureSummaryField();
                    if (targetField.empty()) {
                        appendLog("question_launcher_blocked question=conservation reason=no_fields_available");
                        break;
                    }

                    ProbeDefinition definition;
                    definition.id = targetField + "_conservation_probe";
                    definition.kind = ProbeKind::GlobalScalar;
                    definition.variableName = targetField;
                    std::string addMsg;
                    runtime_.addProbe(definition, addMsg);
                    appendLog(addMsg);
                    focusActiveViewportOnField(targetField);

                    std::string summarizeMsg;
                    runtime_.summarizeField(targetField, summarizeMsg);
                    appendLog("question_launcher_result question=conservation target=" + targetField);
                    appendLog(summarizeMsg);
                    break;
                }
                case 3: {
                    std::vector<CheckpointInfo> checkpointRecords;
                    std::string checkpointMsg;
                    if (runtime_.checkpointRecords(checkpointRecords, checkpointMsg) && !checkpointRecords.empty()) {
                        const CheckpointInfo* selected = nullptr;
                        if (panel_.checkpointSelectedLabel[0] != '\0') {
                            const std::string selectedLabel = panel_.checkpointSelectedLabel;
                            const auto selectedIt = std::find_if(
                                checkpointRecords.begin(),
                                checkpointRecords.end(),
                                [&](const CheckpointInfo& checkpoint) { return checkpoint.label == selectedLabel; });
                            if (selectedIt != checkpointRecords.end()) {
                                selected = &(*selectedIt);
                            }
                        }
                        if (selected == nullptr) {
                            selected = &checkpointRecords.front();
                        }

                        std::snprintf(panel_.checkpointSelectedLabel, sizeof(panel_.checkpointSelectedLabel), "%s", selected->label.c_str());
                        RuntimeCheckpoint currentCheckpoint{};
                        std::string currentMsg;
                        if (runtime_.captureCheckpoint(currentCheckpoint, currentMsg, false /* computeHash */)) {
                            const auto& currentSnapshot = currentCheckpoint.stateSnapshot;
                            std::ostringstream compare;
                            compare << "question_launcher_result question=checkpoint_difference"
                                    << " checkpoint=" << selected->label
                                    << " step_delta=" << static_cast<long long>(currentSnapshot.header.stepIndex) - static_cast<long long>(selected->stepIndex)
                                    << " hash_match=" << (currentSnapshot.stateHash == selected->stateHash ? "yes" : "no")
                                    << " profile_match=" << (currentCheckpoint.profileFingerprint == selected->profileFingerprint ? "yes" : "no")
                                    << " run_match=" << (currentCheckpoint.runSignature.identityHash() == selected->runIdentityHash ? "yes" : "no");
                            appendLog(compare.str());
                        } else {
                            appendLog(currentMsg);
                        }
                    } else {
                        appendLog(checkpointMsg.empty() ? "question_launcher_blocked question=checkpoint_difference reason=no_checkpoints" : checkpointMsg);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(
                "Builds an analysis workflow from a question and logs a concise result summary.\n"
                "The launcher auto-configures probes, view focus, and checkpoint comparison context when available.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (PrimaryButton("Recipe: Track global trend", ImVec2(-1.0f, 24.0f))) {
            if (!viz_.fieldNames.empty()) {
                const std::string trackedField = panel_.summaryVariable[0] != '\0'
                    ? std::string(panel_.summaryVariable)
                    : viz_.fieldNames.front();
                std::snprintf(panel_.summaryVariable, sizeof(panel_.summaryVariable), "%s", trackedField.c_str());

                ProbeDefinition definition;
                definition.id = trackedField + "_global_trend";
                definition.kind = ProbeKind::GlobalScalar;
                definition.variableName = trackedField;

                std::string addMsg;
                runtime_.addProbe(definition, addMsg);
                appendLog(addMsg);
                appendLog("analysis_recipe_applied type=global_trend variable=" + trackedField);
            } else {
                appendLog("analysis_recipe_blocked reason=no_fields_available");
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Creates a global probe for the selected summary variable (or first available field).\nThen use Time-Series Probes to inspect trend and export CSV.");
        }

        if (SecondaryButton("Recipe: Compare with latest checkpoint", ImVec2(-1.0f, 24.0f))) {
            std::vector<CheckpointInfo> checkpointRecords;
            std::string checkpointMsg;
            if (runtime_.checkpointRecords(checkpointRecords, checkpointMsg) && !checkpointRecords.empty()) {
                RuntimeCheckpoint currentCheckpoint{};
                std::string currentMsg;
                if (runtime_.captureCheckpoint(currentCheckpoint, currentMsg, false /* computeHash */)) {
                    const auto& latest = checkpointRecords.back();
                    const auto& currentSnapshot = currentCheckpoint.stateSnapshot;
                    std::ostringstream compare;
                    compare << "analysis_checkpoint_compare"
                            << " selected=" << latest.label
                            << " step_delta=" << static_cast<long long>(currentSnapshot.header.stepIndex) - static_cast<long long>(latest.stepIndex)
                            << " hash_match=" << (currentSnapshot.stateHash == latest.stateHash ? "yes" : "no")
                            << " profile_match=" << (currentCheckpoint.profileFingerprint == latest.profileFingerprint ? "yes" : "no");
                    appendLog(compare.str());
                } else {
                    appendLog(currentMsg);
                }
            } else {
                appendLog(checkpointMsg.empty() ? "analysis_checkpoint_compare_blocked reason=no_checkpoints" : checkpointMsg);
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Logs a compact current-vs-latest checkpoint comparison (step delta, hash/profile match).\nUse In-Memory Checkpoints for detailed inspection and restore actions.");
        }
    }
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(0);
    if (ImGui::CollapsingHeader("Time-Series Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawTimeSeriesSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(1);
    if (ImGui::CollapsingHeader("Histogram & Distribution", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawHistogramSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(2);
    if (ImGui::CollapsingHeader("Constraint Violation Monitor", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawConstraintMonitorSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Field Summary
    PushSectionTint(3);
    if (ImGui::CollapsingHeader("Field Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawFieldSummarySection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Conservation Metrics
    PushSectionTint(4);
    if (ImGui::CollapsingHeader("Conservation & Stability Metrics")) {
        drawConservationSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Runtime Metrics
    PushSectionTint(5);
    if (ImGui::CollapsingHeader("Runtime Metrics & Performance")) {
        drawMetricsSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Trace / Event Log
    PushSectionTint(6);
    if (ImGui::CollapsingHeader("Simulation Trace Log")) {
        drawTraceSection();
    }
    PopSectionTint();

    ImGui::EndChild();
}

// Draws field summary section.
void drawFieldSummarySection() {
    // Field selector
    if (!viz_.fieldNames.empty()) {
        ImGui::SetNextItemWidth(-1.0f);
        const char* summaryPreview = panel_.summaryVariable[0] != '\0' ? panel_.summaryVariable : "<select variable>";
        if (ImGui::BeginCombo("##sumvar", summaryPreview)) {
            for (const auto& fn : viz_.fieldNames) {
                if (ImGui::Selectable(fn.c_str(),
                        fn == std::string(panel_.summaryVariable))) {
                    std::snprintf(panel_.summaryVariable,
                        sizeof(panel_.summaryVariable), "%s", fn.c_str());
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::InputText("##sumvar", panel_.summaryVariable, sizeof(panel_.summaryVariable));
    }
    ImGui::SameLine();
    if (PrimaryButton("Query##sum", ImVec2(60.0f, 24.0f))) {
        std::string msg;
        runtime_.summarizeField(panel_.summaryVariable, msg);
        appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Compute and log min / max / average / valid count for this field.");

    // Live summary from cached snapshot
    if (viz_.hasCachedCheckpoint && panel_.summaryVariable[0] != '\0') {
        const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
        auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& f) {
            return f.spec.name == std::string(panel_.summaryVariable);
        });
        if (it != fields.end()) {
            const auto sum = app::summarizeField(*it);
            ImGui::Columns(4, "sumcols", false);
            ImGui::TextDisabled("valid"); ImGui::NextColumn();
            ImGui::TextDisabled("invalid"); ImGui::NextColumn();
            ImGui::TextDisabled("min"); ImGui::NextColumn();
            ImGui::TextDisabled("max"); ImGui::NextColumn();
            ImGui::Text("%zu", sum.validCount); ImGui::NextColumn();
            ImGui::Text("%zu", sum.invalidCount); ImGui::NextColumn();
            ImGui::Text("%.4f", sum.minValue); ImGui::NextColumn();
            ImGui::Text("%.4f", sum.maxValue); ImGui::NextColumn();
            ImGui::Columns(1);
            ImGui::Text("avg: %.6f", sum.average);
        } else {
            ImGui::TextDisabled("Field not found in current snapshot.");
        }
    }
    ImGui::Spacing();
    if (SecondaryButton("List all fields", ImVec2(-1.0f, 22.0f))) {
        std::string msg;
        runtime_.listFields(msg);
        appendLog(msg);
    }
}

// Draws conservation section for constraint monitoring.
void drawConservationSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Run the simulation to see conservation metrics.");
        return;
    }
    std::string msg;
    runtime_.status(msg);
    ImGui::TextWrapped("%s", msg.c_str());
}

// Draws metrics section for performance statistics.
void drawMetricsSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Run the simulation to see metrics.");
        return;
    }

    drawRuntimeIntentControls("metrics");

    std::string msg;
    runtime_.metrics(msg);
    ImGui::TextWrapped("%s", msg.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Rendering:");
    ImGui::Columns(2, "rendcols", false);
    ImGui::TextDisabled("FPS:"); ImGui::NextColumn();
    ImGui::Text("%.1f", ImGui::GetIO().Framerate); ImGui::NextColumn();
    ImGui::TextDisabled("Snapshot time:"); ImGui::NextColumn();
    ImGui::Text("%.2f ms", viz_.lastSnapshotDurationMs); ImGui::NextColumn();
    ImGui::TextDisabled("Batch step time:"); ImGui::NextColumn();
    ImGui::Text("%.2f ms", simulationLastBatchDurationMs_.load()); ImGui::NextColumn();
    ImGui::TextDisabled("Display interval:"); ImGui::NextColumn();
    ImGui::Text("Every %d step(s) or %.0f ms", std::max(1, viz_.displayRefreshEveryNSteps), displayRefreshLatencyCapMs()); ImGui::NextColumn();
    ImGui::Columns(1);

    ImGui::Spacing();
    checkboxWithHint("Unlimited simulation speed##metrics", &viz_.unlimitedSimSpeed,
        "Run the simulation thread unthrottled.\n"
        "Disable to yield between batches and reduce CPU pressure.");
    sliderIntWithHint("Target display refresh (Hz)##metrics", &viz_.displayTargetRefreshHz, 15, 240,
        "Upper target refresh rate for viewport updates during simulation.\n"
        "Higher values reduce visual latency but can increase snapshot workload.");
    checkboxWithHint("Refresh runtime view on each state change##metrics", &viz_.displayRefreshOnStateChange,
        "When enabled, request a display snapshot after each committed simulation batch.\n"
        "Disable only if raw simulation throughput is the priority.");
    sliderIntWithHint("Steps between display updates##metrics", &viz_.displayRefreshEveryNSteps, 1, 1000,
        "Refresh the display after every N simulation steps.\n"
        "A live latency cap keeps the display responsive even when N is large.");
    const float estStepsPerSec = estimatedSimulationStepsPerSecond();
    const float estRefreshesPerSec = estimatedDisplayRefreshesPerSecond();
    const float actualRefreshesPerSec = estimatedActualDisplayRefreshesPerSecond();
    if (estStepsPerSec > 0.0f) {
        ImGui::TextDisabled("Estimated throughput: %.0f steps/sec, %.1f display updates/sec (<= %.0f ms latency)",
            estStepsPerSec, estRefreshesPerSec, displayRefreshLatencyCapMs());
        if (actualRefreshesPerSec > 0.0f) {
            ImGui::TextDisabled("Observed runtime-view refresh: %.1f updates/sec", actualRefreshesPerSec);
        }
    }
    checkboxWithHint("Adaptive render sampling", &viz_.adaptiveSampling,
        "Automatically skip cells when zoomed out to keep rendering fast.");
    if (!viz_.adaptiveSampling) {
        sliderIntWithHint("Manual stride", &viz_.manualSamplingStride, 1, 64,
            "Render every N-th cell (1 = all cells).");
    }
    sliderIntWithHint("Max rendered cells", &viz_.maxRenderedCells, 1000, 4000000,
        "Hard cap on total cells drawn per frame across all viewports.");
}

// Draws trace section for application log.
void drawTraceSection() {
    ImGui::TextDisabled("Application log:");
    ImGui::BeginChild("AppLog", ImVec2(0, 150.0f), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGuiListClipper lc;
    lc.Begin(static_cast<int>(logs_.size()));
    while (lc.Step()) {
        for (int i = lc.DisplayStart; i < lc.DisplayEnd; ++i)
            ImGui::TextUnformatted(logs_[static_cast<std::size_t>(i)].c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    if (SecondaryButton("Clear log", ImVec2(90.0f, 20.0f))) logs_.clear();
}

// Draws time series section for probe data.
void drawTimeSeriesSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to record probe time-series.");
        return;
    }

    static char probeId[128] = "temperature_probe";
    static int probeKindIndex = 0;
    static int probeFieldIndex = 0;
    static int probeCellX = 0;
    static int probeCellY = 0;
    static int regionMinX = 0;
    static int regionMinY = 0;
    static int regionMaxX = 0;
    static int regionMaxY = 0;
    static int selectedProbeIndex = 0;
    static char probeCsvFile[128] = "timeseries_probes.csv";

    const int maxX = std::max(0, panel_.gridWidth - 1);
    const int maxY = std::max(0, panel_.gridHeight - 1);

    if (!viz_.fieldNames.empty()) {
        probeFieldIndex = std::clamp(probeFieldIndex, 0, static_cast<int>(viz_.fieldNames.size() - 1));
    }

    static constexpr const char* kProbeKinds[] = {"Global", "Cell", "Region average"};
    ImGui::InputText("Probe id", probeId, sizeof(probeId));
    ImGui::Combo("Probe kind", &probeKindIndex, kProbeKinds, static_cast<int>(std::size(kProbeKinds)));

    if (!viz_.fieldNames.empty()) {
        if (ImGui::BeginCombo("Variable##probeVariable", viz_.fieldNames[static_cast<std::size_t>(probeFieldIndex)].c_str())) {
            for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), i == probeFieldIndex)) {
                    probeFieldIndex = i;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("No fields available.");
    }

    if (probeKindIndex == static_cast<int>(ProbeKind::CellScalar)) {
        NumericSliderPairInt("Cell X##probe", &probeCellX, 0, maxX, "%d", 55.0f);
        NumericSliderPairInt("Cell Y##probe", &probeCellY, 0, maxY, "%d", 55.0f);
    } else if (probeKindIndex == static_cast<int>(ProbeKind::RegionAverage)) {
        NumericSliderPairInt("Region min X", &regionMinX, 0, maxX, "%d", 55.0f);
        NumericSliderPairInt("Region min Y", &regionMinY, 0, maxY, "%d", 55.0f);
        NumericSliderPairInt("Region max X", &regionMaxX, 0, maxX, "%d", 55.0f);
        NumericSliderPairInt("Region max Y", &regionMaxY, 0, maxY, "%d", 55.0f);
        regionMaxX = std::max(regionMaxX, regionMinX);
        regionMaxY = std::max(regionMaxY, regionMinY);
    }

    if (PrimaryButton("Add probe", ImVec2(120.0f, 24.0f))) {
        ProbeDefinition definition;
        definition.id = probeId;
        definition.kind = static_cast<ProbeKind>(std::clamp(probeKindIndex, 0, 2));
        if (!viz_.fieldNames.empty()) {
            definition.variableName = viz_.fieldNames[static_cast<std::size_t>(probeFieldIndex)];
        }
        definition.cell = Cell{static_cast<std::uint32_t>(std::max(0, probeCellX)), static_cast<std::uint32_t>(std::max(0, probeCellY))};
        definition.region.min = Cell{static_cast<std::uint32_t>(std::max(0, regionMinX)), static_cast<std::uint32_t>(std::max(0, regionMinY))};
        definition.region.max = Cell{static_cast<std::uint32_t>(std::max(0, regionMaxX)), static_cast<std::uint32_t>(std::max(0, regionMaxY))};

        std::string message;
        runtime_.addProbe(definition, message);
        appendLog(message);
    }
    ImGui::SameLine();
    if (SecondaryButton("Clear probes", ImVec2(120.0f, 24.0f))) {
        std::string message;
        runtime_.clearProbes(message);
        appendLog(message);
        selectedProbeIndex = 0;
    }

    std::vector<ProbeDefinition> definitions;
    std::string defMessage;
    runtime_.probeDefinitions(definitions, defMessage);

    ImGui::Separator();
    ImGui::TextDisabled("Active probes: %d", static_cast<int>(definitions.size()));
    if (definitions.empty()) {
        ImGui::TextDisabled("No probes configured.");
        return;
    }

    selectedProbeIndex = std::clamp(selectedProbeIndex, 0, static_cast<int>(definitions.size() - 1));
    if (ImGui::BeginCombo("Probe series", definitions[static_cast<std::size_t>(selectedProbeIndex)].id.c_str())) {
        for (int i = 0; i < static_cast<int>(definitions.size()); ++i) {
            const auto& probe = definitions[static_cast<std::size_t>(i)];
            if (ImGui::Selectable(probe.id.c_str(), i == selectedProbeIndex)) {
                selectedProbeIndex = i;
            }
        }
        ImGui::EndCombo();
    }

    if (SecondaryButton("Remove selected", ImVec2(140.0f, 24.0f))) {
        std::string message;
        runtime_.removeProbe(definitions[static_cast<std::size_t>(selectedProbeIndex)].id, message);
        appendLog(message);
        selectedProbeIndex = 0;
    }

    ProbeSeries series;
    std::string seriesMessage;
    if (!runtime_.probeSeries(definitions[static_cast<std::size_t>(selectedProbeIndex)].id, series, seriesMessage)) {
        ImGui::TextDisabled("%s", seriesMessage.c_str());
        return;
    }

    ImGui::TextDisabled(
        "Variable=%s kind=%s",
        series.definition.variableName.c_str(),
        probeKindToString(series.definition.kind).c_str());

    if (series.samples.empty()) {
        ImGui::TextDisabled("Probe has no samples yet.");
    } else {
        std::vector<float> values;
        values.reserve(series.samples.size());
        for (const auto& sample : series.samples) {
            values.push_back(sample.value);
        }

        ImGui::PlotLines(
            "##ProbeSeriesPlot",
            values.data(),
            static_cast<int>(values.size()),
            0,
            nullptr,
            FLT_MAX,
            FLT_MAX,
            ImVec2(-1.0f, 140.0f));

        const auto stats = ProbeManager::computeStatistics(series);
        ImGui::Text(
            "samples=%llu min=%.5f max=%.5f mean=%.5f std=%.5f last=%.5f",
            static_cast<unsigned long long>(stats.count),
            stats.minValue,
            stats.maxValue,
            static_cast<float>(stats.mean),
            static_cast<float>(stats.stddev),
            stats.lastValue);
    }

    ImGui::InputText("CSV file##probeCsv", probeCsvFile, sizeof(probeCsvFile));
    if (SecondaryButton("Export probes CSV", ImVec2(160.0f, 24.0f))) {
        std::vector<ProbeSeries> allSeries;
        allSeries.reserve(definitions.size());
        for (const auto& definition : definitions) {
            ProbeSeries probeSeries;
            std::string message;
            if (runtime_.probeSeries(definition.id, probeSeries, message)) {
                allSeries.push_back(std::move(probeSeries));
            }
        }

        std::string message;
        const auto outputPath = std::filesystem::path("checkpoints") / "analysis" / probeCsvFile;
        if (saveProbeSeriesCsv(allSeries, outputPath, message)) {
            appendLog(message);
        } else {
            appendLog(message);
        }
    }
}

// Draws histogram section for field distribution.
void drawHistogramSection() {
    if (!viz_.hasCachedCheckpoint) {
        ImGui::TextDisabled("No snapshot available yet.");
        return;
    }

    static int selectedFieldIndex = 0;
    static int binCount = 50;
    static int normalizationMode = static_cast<int>(HistogramNormalization::Count);

    const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
    if (fields.empty()) {
        ImGui::TextDisabled("Snapshot does not contain fields.");
        return;
    }

    selectedFieldIndex = std::clamp(selectedFieldIndex, 0, static_cast<int>(fields.size() - 1));

    if (ImGui::BeginCombo("Histogram variable", fields[static_cast<std::size_t>(selectedFieldIndex)].spec.name.c_str())) {
        for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
            if (ImGui::Selectable(fields[static_cast<std::size_t>(i)].spec.name.c_str(), i == selectedFieldIndex)) {
                selectedFieldIndex = i;
            }
        }
        ImGui::EndCombo();
    }

    sliderIntWithHint("Bins", &binCount, 10, 200, "Histogram bucket count.");
    static constexpr const char* kNormalizationModes[] = {"Count", "Density", "Max-normalized"};
    ImGui::Combo(
        "Normalization",
        &normalizationMode,
        kNormalizationModes,
        static_cast<int>(std::size(kNormalizationModes)));

    HistogramResult histogram;
    std::string message;
    if (!computeHistogram(
            fields[static_cast<std::size_t>(selectedFieldIndex)],
            binCount,
            static_cast<HistogramNormalization>(std::clamp(normalizationMode, 0, 2)),
            histogram,
            message)) {
        ImGui::TextDisabled("%s", message.c_str());
        return;
    }

    ImGui::PlotHistogram(
        "##Histogram",
        histogram.binValues.data(),
        static_cast<int>(histogram.binValues.size()),
        0,
        nullptr,
        0.0f,
        FLT_MAX,
        ImVec2(-1.0f, 140.0f));

    ImGui::Text(
        "count=%llu min=%.5f max=%.5f mean=%.5f median=%.5f std=%.5f",
        static_cast<unsigned long long>(histogram.stats.count),
        histogram.stats.minValue,
        histogram.stats.maxValue,
        static_cast<float>(histogram.stats.mean),
        static_cast<float>(histogram.stats.median),
        static_cast<float>(histogram.stats.stddev));
    ImGui::Text(
        "skewness=%.5f kurtosis=%.5f",
        static_cast<float>(histogram.stats.skewness),
        static_cast<float>(histogram.stats.kurtosis));
}

// Draws constraint monitor section.
void drawConstraintMonitorSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to track constraint violations.");
        return;
    }
    if (!viz_.hasCachedCheckpoint) {
        ImGui::TextDisabled("No snapshot available yet.");
        return;
    }

    const auto currentStep = viz_.cachedCheckpoint.stateSnapshot.header.stepIndex;
    const auto currentTime = static_cast<float>(viz_.cachedCheckpoint.stateSnapshot.header.timestampTicks);

    if (recordedDiagnosticsStep_ != std::numeric_limits<std::uint64_t>::max() && currentStep < recordedDiagnosticsStep_) {
        constraintMonitor_.clear();
    }
    if (currentStep != recordedDiagnosticsStep_) {
        StepDiagnostics diagnostics;
        std::string message;
        if (runtime_.lastStepDiagnostics(diagnostics, message)) {
            constraintMonitor_.recordStep(diagnostics, currentStep, currentTime);
            recordedDiagnosticsStep_ = currentStep;
        }
    }

    const auto& history = constraintMonitor_.history();
    ImGui::Text(
        "current_step_violations=%llu history_size=%llu",
        static_cast<unsigned long long>(constraintMonitor_.violationsThisStep()),
        static_cast<unsigned long long>(history.size()));

    if (SecondaryButton("Clear monitor history", ImVec2(160.0f, 24.0f))) {
        constraintMonitor_.clear();
        recordedDiagnosticsStep_ = std::numeric_limits<std::uint64_t>::max();
    }

    std::unordered_map<std::string, std::size_t> variableCounts;
    std::unordered_map<std::uint64_t, bool> stepFlags;
    for (const auto& record : history) {
        variableCounts[record.variable] += 1;
        stepFlags[record.step] = true;
    }

    std::pair<std::string, std::size_t> topVariable{"", 0};
    for (const auto& [variable, count] : variableCounts) {
        if (count > topVariable.second) {
            topVariable = {variable, count};
        }
    }

    if (!history.empty()) {
        ImGui::Text(
            "steps_with_violations=%llu top_variable=%s(%llu)",
            static_cast<unsigned long long>(stepFlags.size()),
            topVariable.first.empty() ? "n/a" : topVariable.first.c_str(),
            static_cast<unsigned long long>(topVariable.second));
    }

    ImGui::BeginChild("ConstraintHistory", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const auto& grid = viz_.cachedCheckpoint.stateSnapshot.grid;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        const auto& record = *it;
        const std::uint32_t x = (grid.width > 0u) ? static_cast<std::uint32_t>(record.cellIndex % grid.width) : 0u;
        const std::uint32_t y = (grid.width > 0u) ? static_cast<std::uint32_t>(record.cellIndex / grid.width) : 0u;
        ImGui::Text(
            "step=%llu var=%s cell=(%u,%u) severity=%s",
            static_cast<unsigned long long>(record.step),
            record.variable.c_str(),
            x,
            y,
            record.severity.c_str());
    }
    ImGui::EndChild();
}

//
// Tab: Diagnostics
//
// Draws diagnostics tab with runtime information.
void drawDiagnosticsTab() {
    ImGui::BeginChild("DiagTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (!runtime_.isRunning()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Diagnostics are available while a simulation is running.");
        ImGui::EndChild();
        return;
    }

    // Admission report
    PushSectionTint(0);
    if (ImGui::CollapsingHeader("Admission Report", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawAdmissionSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Step diagnostics
    PushSectionTint(1);
    if (ImGui::CollapsingHeader("Last Step Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawStepDiagSection();
    }
    PopSectionTint();
    ImGui::Spacing();

    // Ordering log
    PushSectionTint(2);
    if (ImGui::CollapsingHeader("Step Ordering Log")) {
        drawOrderingLogSection();
    }
    PopSectionTint();

    ImGui::Spacing();

    PushSectionTint(3);
    if (ImGui::CollapsingHeader("Notification History")) {
        drawNotificationHistorySection();
    }
    PopSectionTint();

    ImGui::EndChild();
}

// Draws admission section for model validation.
void drawAdmissionSection() {
    std::string statusMsg;
    runtime_.status(statusMsg);
    ImGui::TextWrapped("%s", statusMsg.c_str());
}

// Draws step diagnostics section.
void drawStepDiagSection() {
    std::string metricsMsg;
    runtime_.metrics(metricsMsg);
    ImGui::TextWrapped("%s", metricsMsg.c_str());
}

// Draws ordering log section.
void drawOrderingLogSection() {
    ImGui::BeginChild("OrdLog", ImVec2(0, 160.0f), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    std::string statusMsg;
    runtime_.status(statusMsg);
    ImGui::TextWrapped("%s", statusMsg.c_str());
    ImGui::EndChild();
}

// Draws notification history section.
void drawNotificationHistorySection() {
    if (toastHistory_.empty()) {
        ImGui::TextDisabled("No notifications recorded yet.");
        return;
    }

    if (SecondaryButton("Clear notification history", ImVec2(220.0f, 24.0f))) {
        toastHistory_.clear();
        return;
    }

    ImGui::BeginChild("NotificationHistory", ImVec2(0.0f, 170.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (auto it = toastHistory_.rbegin(); it != toastHistory_.rend(); ++it) {
        const auto& toast = *it;
        const char* level = "Info";
        ImVec4 color = ImVec4(0.45f, 0.70f, 0.95f, 0.95f);
        if (toast.level == ToastLevel::Error) {
            level = "Error";
            color = ImVec4(0.95f, 0.35f, 0.35f, 0.95f);
        } else if (toast.level == ToastLevel::Warning) {
            level = "Warning";
            color = ImVec4(0.95f, 0.75f, 0.35f, 0.95f);
        } else if (toast.level == ToastLevel::Success) {
            level = "Success";
            color = ImVec4(0.45f, 0.85f, 0.50f, 0.95f);
        }

        ImGui::TextColored(color, "[%s]", level);
        ImGui::SameLine();
        ImGui::TextUnformatted(toast.title.empty() ? "Notice" : toast.title.c_str());
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(toast.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
    }
    ImGui::EndChild();
}

//
// Tab: System
//
// Draws system tab with configuration.
void drawSystemTab() {
    ImGui::BeginChild("SysTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Profiles
    PushSectionTint(0);
    if (ImGui::CollapsingHeader("Launch Profiles", ImGuiTreeNodeFlags_DefaultOpen))
        drawProfilesSection();
    PopSectionTint();
    ImGui::Spacing();

    // Grid / Generation (for restart)
    PushSectionTint(1);
    if (ImGui::CollapsingHeader("Grid & Generation Settings"))
        drawGridAndGenerationSection();
    PopSectionTint();
    ImGui::Spacing();

    // Accessibility
    PushSectionTint(2);
    if (ImGui::CollapsingHeader("Accessibility & UI"))
        drawAccessibilitySection();
    PopSectionTint();
    ImGui::Spacing();

    // Keyboard shortcuts reference
    PushSectionTint(3);
    if (ImGui::CollapsingHeader("Keyboard Shortcuts"))
        drawShortcutsSection();
    PopSectionTint();

    ImGui::EndChild();
}

// Draws profiles section.
void drawProfilesSection() {
    inputTextWithHint("Name##prof", panel_.profileName, sizeof(panel_.profileName),
        "Profile name for save / load. Letters, digits, '_', '-' only.");
    const float w3 = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (PrimaryButton("Save##prof", ImVec2(w3, 26.0f))) {
        std::string msg; [[maybe_unused]] const bool saved = runtime_.saveProfile(panel_.profileName, msg); appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Save the current launch configuration under this profile name.");
    ImGui::SameLine(0,4);
    if (PrimaryButton("Load##prof", ImVec2(w3, 26.0f))) {
        std::string msg;
        if (runtime_.loadProfile(panel_.profileName, msg)) {
            syncPanelFromConfig();
            refreshFieldNames();
            requestSnapshotRefresh();
        }
        appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Load a previously saved profile. Use Restart to apply it.");
    ImGui::SameLine(0,4);
    if (SecondaryButton("List##prof", ImVec2(-1.0f, 26.0f))) {
        std::string msg; [[maybe_unused]] const bool queried = runtime_.listProfiles(msg); appendLog(msg);
    }
}

// Draws grid and generation section.
void drawGridAndGenerationSection() {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
        "Changes below require Restart (Simulation tab) to take effect.");
    ImGui::Spacing();

    drawGridSetupSection();
    ImGui::Spacing();

    PushSectionTint(7);
    if (ImGui::CollapsingHeader("World Generation Parameters"))
        drawWorldGenerationSection();
    PopSectionTint();
}

// Draws shortcuts section.
void drawShortcutsSection() {
    drawShortcutReferenceTable("shortcols");
}

// Grid setup section (used by System tab)
// Draws grid setup section.
void drawGridSetupSection() {
    PushSectionTint(6);
    if (ImGui::CollapsingHeader("Grid Registration", ImGuiTreeNodeFlags_DefaultOpen)) {
        checkboxWithHint("Manual seed", &panel_.useManualSeed,
            "When disabled, a random seed is generated at world creation.");
        if (panel_.useManualSeed) {
            std::uint64_t manualSeed = panel_.seed;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputScalar("Seed##grid", ImGuiDataType_U64, &manualSeed))
                panel_.seed = std::max<std::uint64_t>(1ull, manualSeed);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Deterministic seed. Same seed + same parameters = identical world.");
        } else {
            ImGui::Text("Seed (auto): %llu", (unsigned long long)panel_.seed);
            if (SecondaryButton("Reroll seed", ImVec2(120.0f, 24.0f)))
                panel_.seed = generateRandomSeed();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Generate a new random seed without creating the world.");
        }

        NumericSliderPairInt("Width (cells)",  &panel_.gridWidth,  1, 4096, "%d", 55.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Grid width in cells. Large grids increase memory and computation time.\nRecommended: 128-512 for interactive use.");
        NumericSliderPairInt("Height (cells)", &panel_.gridHeight, 1, 4096, "%d", 55.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Grid height in cells.");
        if (panel_.gridWidth > 0 && panel_.gridHeight > 0) {
            ImGui::TextDisabled("Total cells: %d  (~%.1f MB float32)",
                panel_.gridWidth * panel_.gridHeight,
                static_cast<float>(panel_.gridWidth) * panel_.gridHeight * 14 * 4 / 1048576.0f);
        }
    }
    PopSectionTint();
}

// World generation section (used by System tab & New World Wizard)
// Draws world generation section.
void drawWorldGenerationSection() {
    const auto& modelCellVars = sessionUi_.selectedModelCellStateVariables;
    auto drawVariableBindingSelector = [&](const char* comboLabel, const char* manualLabel, char* buffer, const std::size_t size, const char* hint) {
        if (!modelCellVars.empty()) {
            const char* preview = buffer[0] != '\0' ? buffer : "<select variable>";
            if (ImGui::BeginCombo(comboLabel, preview)) {
                for (const auto& variable : modelCellVars) {
                    const bool selected = (variable == buffer);
                    if (ImGui::Selectable(variable.c_str(), selected)) {
                        std::snprintf(buffer, size, "%s", variable.c_str());
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", hint);
            }

            inputTextWithHint(manualLabel, buffer, size,
                "Optional manual override. Keep matching selected model variable IDs.");
        } else {
            inputTextWithHint(manualLabel, buffer, size,
                "No model cell-state variables detected; enter a valid variable ID manually.");
        }
    };

    std::vector<ParameterControl> generationParameterControls;
    std::string generationParameterMessage;
    runtime_.parameterControls(generationParameterControls, generationParameterMessage);
    static_cast<void>(generationParameterMessage);

    const auto recommendation = GenerationAdvisor::recommendGenerationMode(
        sessionUi_.selectedModelCatalog,
        generationParameterControls);
    const InitialConditionType refinedRecommendation = refineRecommendedModeForKnownModels(
        sessionUi_.selectedModelCatalog,
        recommendation.recommendedType);
    const auto viableModes = GenerationAdvisor::viableGenerationModes(sessionUi_.selectedModelCatalog);

    const auto isModeViable = [&](const InitialConditionType modeType) {
        return std::find(viableModes.begin(), viableModes.end(), modeType) != viableModes.end();
    };

    const float confidencePct = std::clamp(recommendation.confidence * 100.0f, 0.0f, 100.0f);
    ImGui::TextColored(
        ImVec4(0.62f, 0.82f, 0.95f, 1.0f),
        "Recommended: %s (%.0f%%)",
        generationModeLabel(refinedRecommendation),
        confidencePct);
    ImGui::TextDisabled("%s", humanizeToken(recommendation.rationale).c_str());

    if (SecondaryButton("Apply recommended defaults", ImVec2(-1.0f, 24.0f))) {
        const InitialConditionType runtimeMode = fallbackRuntimeSupportedMode(refinedRecommendation);
        applyGenerationDefaultsForMode(panel_, sessionUi_.selectedModelCatalog, runtimeMode, true);
        applyAutoVariableBindingsForMode(panel_, modelCellVars, runtimeMode);
        viz_.generationPreviewDisplayType = recommendedPreviewDisplayTypeForMode(runtimeMode);
        sessionUi_.generationPreviewSourceIndex = recommendedPreviewSourceForMode(runtimeMode);
        sessionUi_.generationPreviewChannelIndex = findPreferredVariableIndex(
            sessionUi_.selectedModelCatalog,
            modelCellVars,
            {"fire_state", "living", "water", "state", "concentration", "temperature", "vegetation", "velocity", "oxygen"},
            0);
        sessionUi_.generationModeIndex = static_cast<int>(runtimeMode);
        rebuildVariableInitializationSettings(sessionUi_, sessionUi_.selectedModelCatalog);
    }
    DelayedTooltip("Sets generation mode and parameters using model-aware defaults from metadata analysis.");

    checkboxWithHint(
        "Show only viable modes",
        &sessionUi_.generationShowOnlyViableModes,
        "Filters mode list to options that best match the selected model metadata and variable catalog.");

    static constexpr std::array<InitialConditionType, 13> kAllGenerationModes = {
        InitialConditionType::Terrain,
        InitialConditionType::Conway,
        InitialConditionType::GrayScott,
        InitialConditionType::Waves,
        InitialConditionType::Blank,
        InitialConditionType::Voronoi,
        InitialConditionType::Clustering,
        InitialConditionType::SparseRandom,
        InitialConditionType::GradientField,
        InitialConditionType::Checkerboard,
        InitialConditionType::RadialPattern,
        InitialConditionType::MultiScale,
        InitialConditionType::DiffusionLimit};

    auto ensureValidModeSelection = [&]() {
        const InitialConditionType selected = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
        if (!sessionUi_.generationShowOnlyViableModes || isModeViable(selected)) {
            return;
        }
        if (isModeViable(refinedRecommendation)) {
            panel_.initialConditionTypeIndex = static_cast<int>(refinedRecommendation);
            return;
        }
        for (const auto mode : kAllGenerationModes) {
            if (isModeViable(mode)) {
                panel_.initialConditionTypeIndex = static_cast<int>(mode);
                return;
            }
        }
        panel_.initialConditionTypeIndex = static_cast<int>(InitialConditionType::Blank);
    };

    ensureValidModeSelection();

    const InitialConditionType selectedMode = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
    const char* selectedLabel = generationModeLabel(selectedMode);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float generationModeLabelWidth = ImGui::CalcTextSize("Generation Mode").x;
    const float generationModeWidth = std::max(
        180.0f,
        ImGui::GetContentRegionAvail().x - generationModeLabelWidth - style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(generationModeWidth);
    if (ImGui::BeginCombo("Generation Mode", selectedLabel)) {
        for (const auto modeType : kAllGenerationModes) {
            const bool viable = isModeViable(modeType);
            const bool runtimeSupported = isRuntimeSupportedGenerationMode(modeType);
            if (sessionUi_.generationShowOnlyViableModes && !viable) {
                continue;
            }

            std::string entryLabel = generationModeLabel(modeType);
            if (modeType == refinedRecommendation) {
                entryLabel += "  [recommended]";
            }
            if (!viable) {
                entryLabel += "  [low match]";
            }
            if (!runtimeSupported) {
                entryLabel += "  [coming soon]";
            }

            const bool selected = (panel_.initialConditionTypeIndex == static_cast<int>(modeType));
            if (!viable || !runtimeSupported) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Selectable(entryLabel.c_str(), selected)) {
                panel_.initialConditionTypeIndex = static_cast<int>(modeType);
            }
            if (!viable || !runtimeSupported) {
                ImGui::EndDisabled();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled(
        "%s",
        GenerationAdvisor::describeGenerationMode(static_cast<InitialConditionType>(panel_.initialConditionTypeIndex)).c_str());
    ImGui::Separator();

    if (panel_.initialConditionTypeIndex == 0) {
        ImGui::TextUnformatted("Terrain Spectrum");
        sliderFloatWithHint("Base frequency",   &panel_.terrainBaseFrequency,   0.1f, 12.0f, "%.2f",
            "Controls the scale of major terrain features (continents, large hills).\n"
            "Low = few large features. High = many smaller features.");
        sliderFloatWithHint("Detail frequency", &panel_.terrainDetailFrequency, 0.2f, 24.0f, "%.2f",
            "Scale of fine surface detail layered on top of base terrain.\n"
            "Higher = more granular coastlines and ridge detail.");
        sliderFloatWithHint("Warp strength",    &panel_.terrainWarpStrength,    0.0f,  2.0f, "%.2f",
            "Domain warp amount - bends noise coordinates to create more organic shapes.\n"
            "0 = no warp (geometric). 1-2 = naturalistic distortion.");
        sliderFloatWithHint("Amplitude",        &panel_.terrainAmplitude,       0.1f,  3.0f, "%.2f",
            "Vertical elevation contrast. Higher = more dramatic peaks and valleys.");
        sliderFloatWithHint("Ridge mix",        &panel_.terrainRidgeMix,        0.0f,  1.0f, "%.2f",
            "Blend of ridge/mountain noise into the terrain.\n"
            "0 = smooth rolling hills. 1 = sharp ridgelines.");
        ImGui::Separator();
        ImGui::TextUnformatted("Fractal Parameters");
        {
            int oct = panel_.terrainOctaves;
            if (NumericSliderPairInt("Octaves", &oct, 1, 8, "%d", 55.0f)) panel_.terrainOctaves = oct;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Number of layered noise frequencies summed together.\n"
                    "More octaves = more surface detail but slower generation preview.");
        }
        sliderFloatWithHint("Lacunarity",       &panel_.terrainLacunarity, 1.0f, 4.0f, "%.2f",
            "Frequency multiplier between octaves. 2.0 = standard fBm.");
        sliderFloatWithHint("Gain (persistence",&panel_.terrainGain,       0.1f, 0.9f, "%.2f",
            "Amplitude multiplier between octaves. 0.5 = standard fBm.\n"
            "Lower = high frequencies contribute less (smoother).");
        ImGui::Separator();
        ImGui::TextUnformatted("Climate & Hydrology");
        sliderFloatWithHint("Sea level",          &panel_.seaLevel,           0.0f, 1.0f, "%.3f",
            "Elevation threshold that separates land from ocean at initialization.\n"
            "0.5 = roughly half ocean. Lower = more land.");
        sliderFloatWithHint("Polar cooling",      &panel_.polarCooling,       0.0f, 1.5f, "%.2f",
            "Strength of temperature reduction toward the poles (top/bottom edges).\n"
            "Higher = colder poles, stronger latitudinal climate bands.");
        sliderFloatWithHint("Latitude banding",   &panel_.latitudeBanding,    0.0f, 2.0f, "%.2f",
            "Intensity of equatorial-to-polar climate gradients.\n"
            "0 = uniform temperature. 2 = strong equator-to-pole difference.");
        sliderFloatWithHint("Humidity from water",&panel_.humidityFromWater,  0.0f, 1.5f, "%.2f",
            "How strongly initial water coverage drives humidity seeding.\n"
            "Higher = coastal and oceanic areas start much more humid.");
        sliderFloatWithHint("Biome noise",        &panel_.biomeNoiseStrength, 0.0f, 1.0f, "%.2f",
            "Additional spatial noise added to temperature/humidity for biome diversity.\n"
            "0 = purely latitudinal. 1 = maximum regional variation.");
        ImGui::Separator();
        ImGui::TextUnformatted("Island Morphology");
        sliderFloatWithHint("Island density",     &panel_.islandDensity,      0.05f, 0.95f, "%.3f",
            "Probability that any grid cell is part of an island cluster.\n"
            "Low = sparse archipelago. High = dense continent coverage.");
        sliderFloatWithHint("Island falloff",     &panel_.islandFalloff,      0.35f,  4.5f, "%.2f",
            "Sharpness of island edge falloff from center to coast.\n"
            "Low = gentle slopes. High = steep cliffs at coast.");
        sliderFloatWithHint("Coastline sharpness",&panel_.coastlineSharpness, 0.25f,  4.0f, "%.2f",
            "Controls how abruptly terrain drops to sea level at coastlines.\n"
            "Higher = crisper, more defined coasts.");
        sliderFloatWithHint("Archipelago jitter", &panel_.archipelagoJitter,  0.0f,   1.5f, "%.2f",
            "Random offset applied to island center positions.\n"
            "0 = regular spacing. 1.5 = chaotic, irregular archipelago.");
        sliderFloatWithHint("Erosion strength",   &panel_.erosionStrength,    0.0f,   1.0f, "%.2f",
            "Post-processing erosion modulation applied to terrain.\n"
            "Higher = softer, more weathered-looking terrain.");
        sliderFloatWithHint("Shelf depth",        &panel_.shelfDepth,         0.0f,   0.8f, "%.2f",
            "Depth of the continental shelf zone around islands.\n"
            "Higher = wider shallow-water shelf gradient around coasts.");
    } else if (panel_.initialConditionTypeIndex == 1) { // Conway
        drawVariableBindingSelector(
            "Target variable##conway_target_combo",
            "Target variable##conway_target_input",
            panel_.conwayTargetVariable,
            IM_ARRAYSIZE(panel_.conwayTargetVariable),
            "Choose a cell-state variable from the selected model to receive Conway initialization.");
        sliderFloatWithHint("Alive probability", &panel_.conwayAliveProbability, 0.0f, 1.0f, "%.3f",
            "Probability that a cell starts alive (deterministic per seed). ");
        sliderFloatWithHint("Alive value", &panel_.conwayAliveValue, -10.0f, 10.0f, "%.3f",
            "Numeric value written for alive cells.");
        sliderFloatWithHint("Dead value", &panel_.conwayDeadValue, -10.0f, 10.0f, "%.3f",
            "Numeric value written for dead cells.");
        NumericSliderPairInt("Smoothing passes", &panel_.conwaySmoothingPasses, 0, 6, "%d", 110.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Applies deterministic majority smoothing after random seeding.\n"
                              "0 = no smoothing, higher = larger coherent clusters.");
        }
    } else if (panel_.initialConditionTypeIndex == 2) { // Gray-Scott
        drawVariableBindingSelector(
            "Chemical A variable##gray_a_combo",
            "Chemical A variable##gray_a_input",
            panel_.grayScottTargetVariableA,
            IM_ARRAYSIZE(panel_.grayScottTargetVariableA),
            "Primary Gray-Scott field target (U-like). Prefer a cell state scalar concentration field.");
        drawVariableBindingSelector(
            "Chemical B variable##gray_b_combo",
            "Chemical B variable##gray_b_input",
            panel_.grayScottTargetVariableB,
            IM_ARRAYSIZE(panel_.grayScottTargetVariableB),
            "Secondary Gray-Scott field target (V-like). Should usually differ from Chemical A.");
        sliderFloatWithHint("Background A", &panel_.grayScottBackgroundA, -10.0f, 10.0f, "%.3f",
            "Baseline concentration for chemical A.");
        sliderFloatWithHint("Background B", &panel_.grayScottBackgroundB, -10.0f, 10.0f, "%.3f",
            "Baseline concentration for chemical B.");
        sliderFloatWithHint("Spot A", &panel_.grayScottSpotValueA, -10.0f, 10.0f, "%.3f",
            "Chemical A value inside seeded spots.");
        sliderFloatWithHint("Spot B", &panel_.grayScottSpotValueB, -10.0f, 10.0f, "%.3f",
            "Chemical B value inside seeded spots.");
        if (NumericSliderPairInt("Spot count", &panel_.grayScottSpotCount, 1, 50, "%d", 110.0f)) {}
        sliderFloatWithHint("Spot radius", &panel_.grayScottSpotRadius, 0.1f, 100.0f, "%.1f",
            "Radius of each seeded activator spot.");
        sliderFloatWithHint("Spot jitter", &panel_.grayScottSpotJitter, 0.0f, 1.0f, "%.2f",
            "Radius variability per spot for less uniform pattern seeds.");
    } else if (panel_.initialConditionTypeIndex == 3) { // Waves
        drawVariableBindingSelector(
            "Wave variable##waves_combo",
            "Wave variable##waves_input",
            panel_.wavesTargetVariable,
            IM_ARRAYSIZE(panel_.wavesTargetVariable),
            "Choose a cell-state variable to receive wave/drop seeding.");
        sliderFloatWithHint("Baseline", &panel_.waveBaseline, -10.0f, 10.0f, "%.3f",
            "Background value before drop is applied.");
        sliderFloatWithHint("Drop amplitude", &panel_.waveDropAmplitude, -10.0f, 10.0f, "%.3f",
            "Center splash amplitude added on top of baseline.");
        sliderFloatWithHint("Drop radius", &panel_.waveDropRadius, 0.1f, 200.0f, "%.1f",
            "Radius of the initial splash profile.");
        NumericSliderPairInt("Drop count", &panel_.waveDropCount, 1, 16, "%d", 110.0f);
        sliderFloatWithHint("Drop jitter", &panel_.waveDropJitter, 0.0f, 1.0f, "%.2f",
            "Spread of secondary drops around center for multi-source wave starts.");
        sliderFloatWithHint("Ring frequency", &panel_.waveRingFrequency, 0.5f, 4.0f, "%.2f",
            "Oscillation count inside each drop radius. Higher = more ripple rings.");
    } else if (panel_.initialConditionTypeIndex == 4) { // Blank
        ImGui::TextDisabled("Blank mode initializes all runtime fields to zero.");
    } else {
        ImGui::TextDisabled("This generation mode is listed from metadata analysis, but runtime initialization support is not enabled yet.");
        ImGui::TextDisabled("Use Apply recommended defaults to switch to a practical supported mode automatically.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Per-variable initialization (x_i defaults)");
    if (sessionUi_.variableInitializationSettings.empty()) {
        ImGui::TextDisabled("No model cell-state variables detected.");
    } else {
        static constexpr const char* kRestrictionModes[] = {
            "None", "Clamp[min,max]", "Non-negative", "Clamp[-1,1]", "tanh(x)", "sigmoid(x)"};

        const float controlsWidth = ImGui::GetContentRegionAvail().x;
        const bool compactActionLayout = controlsWidth < 560.0f;
        const float actionW = compactActionLayout
            ? std::max(120.0f, (controlsWidth - kS2) * 0.5f)
            : std::max(95.0f, (controlsWidth - (3.0f * kS2)) * 0.25f);

        if (SecondaryButton("Enable all", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                setting.enabled = true;
            }
        }
        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Disable all", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                setting.enabled = false;
            }
        }

        if (!compactActionLayout) {
            ImGui::SameLine(0.0f, kS2);
        }
        if (SecondaryButton("Enable suggested", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                setting.enabled = false;
            }

            const InitialConditionType modeType = static_cast<InitialConditionType>(
                std::clamp(panel_.initialConditionTypeIndex, 0, static_cast<int>(InitialConditionType::DiffusionLimit)));
            std::vector<int> candidates;
            for (int i = 0; i < static_cast<int>(sessionUi_.variableInitializationSettings.size()); ++i) {
                const auto& variableId = sessionUi_.variableInitializationSettings[static_cast<std::size_t>(i)].variableId;
                bool match = false;
                if (modeType == InitialConditionType::Conway) {
                    match = containsToken(variableId, {"state", "alive", "binary", "fire", "vegetation"});
                } else if (modeType == InitialConditionType::GrayScott) {
                    match = containsToken(variableId, {"concentration", "resource", "nutrient", "prey", "predator", "u", "v"});
                } else if (modeType == InitialConditionType::Waves || modeType == InitialConditionType::Terrain) {
                    match = containsToken(variableId, {"water", "height", "surface", "temperature", "humidity", "moisture"});
                } else {
                    match = containsToken(variableId, {"state", "value", "signal", "field"});
                }
                if (match) {
                    candidates.push_back(i);
                }
            }

            if (candidates.empty() && !sessionUi_.variableInitializationSettings.empty()) {
                candidates.push_back(0);
            }

            const int requestedCount = (modeType == InitialConditionType::GrayScott) ? 2 : 1;
            for (int i = 0; i < std::min(requestedCount, static_cast<int>(candidates.size())); ++i) {
                sessionUi_.variableInitializationSettings[static_cast<std::size_t>(candidates[static_cast<std::size_t>(i)])].enabled = true;
            }
        }

        if (compactActionLayout) {
            ImGui::SameLine(0.0f, kS2);
        } else {
            ImGui::SameLine(0.0f, kS2);
        }
        if (SecondaryButton("Enabled -> midpoint", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                if (!setting.enabled) {
                    continue;
                }
                const float lo = std::min(setting.clampMin, setting.clampMax);
                const float hi = std::max(setting.clampMin, setting.clampMax);
                setting.baseValue = 0.5f * (lo + hi);
            }
        }

        const bool compactPairLayout = ImGui::GetContentRegionAvail().x < (240.0f + kS2);
        if (SecondaryButton("Enabled -> zero", ImVec2(120.0f, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                if (setting.enabled) {
                    setting.baseValue = 0.0f;
                }
            }
        }
        if (!compactPairLayout) {
            ImGui::SameLine(0.0f, kS2);
        }
        if (SecondaryButton("Enabled -> one", ImVec2(120.0f, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                if (setting.enabled) {
                    setting.baseValue = 1.0f;
                }
            }
        }

        const float previewSliderLabelWidth = ImGui::CalcTextSize("Preview focus variable").x;
        const float previewSliderWidth = std::max(
            160.0f,
            ImGui::GetContentRegionAvail().x - previewSliderLabelWidth - style.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(previewSliderWidth);
        const int previewMax = static_cast<int>(sessionUi_.variableInitializationSettings.size()) - 1;
        if (previewMax >= 0) {
            ImGui::SliderInt("Preview focus variable", &sessionUi_.generationPreviewChannelIndex, 0, previewMax);
        }

        ImGui::BeginChild("VariableInitList", ImVec2(-1.0f, 190.0f), true);
        for (int i = 0; i < static_cast<int>(sessionUi_.variableInitializationSettings.size()); ++i) {
            auto& setting = sessionUi_.variableInitializationSettings[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::Checkbox("##enabled", &setting.enabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(setting.variableId.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Focus preview")) {
                sessionUi_.generationPreviewChannelIndex = i;
            }

            if (setting.enabled) {
                ImGui::Indent();
                NumericSliderPair("Base value", &setting.baseValue, -10.0f, 10.0f, "%.3f", 95.0f);
                ImGui::SetNextItemWidth(190.0f);
                ImGui::Combo("Restriction", &setting.restrictionMode, kRestrictionModes, static_cast<int>(std::size(kRestrictionModes)));
                if (setting.restrictionMode == 1) {
                    NumericSliderPair("Clamp min", &setting.clampMin, -10.0f, 10.0f, "%.3f", 95.0f);
                    NumericSliderPair("Clamp max", &setting.clampMax, -10.0f, 10.0f, "%.3f", 95.0f);
                    if (setting.clampMin > setting.clampMax) {
                        std::swap(setting.clampMin, setting.clampMax);
                    }
                }
                ImGui::Unindent();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

// Accessibility
// Draws accessibility section.
void drawAccessibilitySection() {
    bool styleChanged = false;
    bool rebuildFonts = false;

    if (sliderFloatWithHint("UI scale",   &accessibility_.uiScale,   0.75f, 3.0f, "%.2f",
        "Global UI scale multiplier. Affects all controls and text."))
        styleChanged = true;
    if (sliderFloatWithHint("Font size",  &accessibility_.fontSizePx, 10.0f, 32.0f, "%.1f",
        "Base font size in pixels.")) {
        styleChanged = true; rebuildFonts = true;
    }
    if (checkboxWithHint("High contrast",       &accessibility_.highContrast,
        "Boost UI contrast for visibility in bright environments.")) styleChanged = true;
    if (checkboxWithHint("Keyboard navigation", &accessibility_.keyboardNav,
        "Enable ImGui keyboard navigation (Tab to cycle, Enter to activate).")) styleChanged = true;
    if (checkboxWithHint("Focus indicators",    &accessibility_.focusIndicators,
        "Show yellow highlight around the currently focused control.")) styleChanged = true;
    checkboxWithHint("Reduce motion",           &accessibility_.reduceMotion,
        "Disable non-essential animation effects (overlay icons, etc.).");

    if (styleChanged) applyTheme(rebuildFonts);
}

//
// Helpers
//
// Syncs panel state from runtime configuration.
void syncPanelFromConfig() {
    const auto& c = runtime_.config();
    panel_.seed           = c.seed;
    panel_.useManualSeed  = true;
    panel_.gridWidth      = static_cast<int>(c.grid.width);
    panel_.gridHeight     = static_cast<int>(c.grid.height);
    panel_.tierIndex      = (c.tier == ModelTier::A) ? 0 : (c.tier == ModelTier::B) ? 1 : 2;
    const std::string tmp = app::temporalPolicyToString(c.temporalPolicy);
    panel_.temporalIndex  = (tmp == "uniform") ? 0 : (tmp == "phased") ? 1 : 2;

    panel_.initialConditionTypeIndex = static_cast<int>(c.initialConditions.type);

    // Sync terrain
    panel_.terrainBaseFrequency   = c.initialConditions.terrain.terrainBaseFrequency;
    panel_.terrainDetailFrequency = c.initialConditions.terrain.terrainDetailFrequency;
    panel_.terrainWarpStrength    = c.initialConditions.terrain.terrainWarpStrength;
    panel_.terrainAmplitude       = c.initialConditions.terrain.terrainAmplitude;
    panel_.terrainRidgeMix        = c.initialConditions.terrain.terrainRidgeMix;
    panel_.terrainOctaves         = c.initialConditions.terrain.terrainOctaves;
    panel_.terrainLacunarity      = c.initialConditions.terrain.terrainLacunarity;
    panel_.terrainGain            = c.initialConditions.terrain.terrainGain;
    panel_.seaLevel               = c.initialConditions.terrain.seaLevel;
    panel_.polarCooling           = c.initialConditions.terrain.polarCooling;
    panel_.latitudeBanding        = c.initialConditions.terrain.latitudeBanding;
    panel_.humidityFromWater      = c.initialConditions.terrain.humidityFromWater;
    panel_.biomeNoiseStrength     = c.initialConditions.terrain.biomeNoiseStrength;
    panel_.islandDensity          = c.initialConditions.terrain.islandDensity;
    panel_.islandFalloff          = c.initialConditions.terrain.islandFalloff;
    panel_.coastlineSharpness     = c.initialConditions.terrain.coastlineSharpness;
    panel_.archipelagoJitter      = c.initialConditions.terrain.archipelagoJitter;
    panel_.erosionStrength        = c.initialConditions.terrain.erosionStrength;
    panel_.shelfDepth             = c.initialConditions.terrain.shelfDepth;

    std::snprintf(panel_.conwayTargetVariable, sizeof(panel_.conwayTargetVariable), "%s", c.initialConditions.conway.targetVariable.c_str());
    panel_.conwayAliveProbability = c.initialConditions.conway.aliveProbability;
    panel_.conwayAliveValue = c.initialConditions.conway.aliveValue;
    panel_.conwayDeadValue = c.initialConditions.conway.deadValue;
    panel_.conwaySmoothingPasses = c.initialConditions.conway.smoothingPasses;

    std::snprintf(panel_.grayScottTargetVariableA, sizeof(panel_.grayScottTargetVariableA), "%s", c.initialConditions.grayScott.targetVariableA.c_str());
    std::snprintf(panel_.grayScottTargetVariableB, sizeof(panel_.grayScottTargetVariableB), "%s", c.initialConditions.grayScott.targetVariableB.c_str());
    panel_.grayScottBackgroundA = c.initialConditions.grayScott.backgroundA;
    panel_.grayScottBackgroundB = c.initialConditions.grayScott.backgroundB;
    panel_.grayScottSpotValueA = c.initialConditions.grayScott.spotValueA;
    panel_.grayScottSpotValueB = c.initialConditions.grayScott.spotValueB;
    panel_.grayScottSpotCount = c.initialConditions.grayScott.spotCount;
    panel_.grayScottSpotRadius = c.initialConditions.grayScott.spotRadius;
    panel_.grayScottSpotJitter = c.initialConditions.grayScott.spotJitter;

    std::snprintf(panel_.wavesTargetVariable, sizeof(panel_.wavesTargetVariable), "%s", c.initialConditions.waves.targetVariable.c_str());
    panel_.waveBaseline = c.initialConditions.waves.baseline;
    panel_.waveDropAmplitude = c.initialConditions.waves.dropAmplitude;
    panel_.waveDropRadius = c.initialConditions.waves.dropRadius;
    panel_.waveDropCount = c.initialConditions.waves.dropCount;
    panel_.waveDropJitter = c.initialConditions.waves.dropJitter;
    panel_.waveRingFrequency = c.initialConditions.waves.ringFrequency;
    panel_.playbackSpeed = runtime_.playbackSpeed();
    panel_.playbackSpeedDirty = false;
    panel_.parameterValueDirty = false;
    panel_.selectedParameterName[0] = '\0';
}

// Applies panel settings to runtime configuration.
void applyConfigFromPanel() {
    app::LaunchConfig cfg = runtime_.config();
    cfg.seed        = panel_.seed;
    cfg.grid        = GridSpec{
        static_cast<std::uint32_t>(std::clamp(panel_.gridWidth,  1, 4096)),
        static_cast<std::uint32_t>(std::clamp(panel_.gridHeight, 1, 4096))};
    cfg.tier        = (panel_.tierIndex == 0) ? ModelTier::A
                    : (panel_.tierIndex == 1) ? ModelTier::B : ModelTier::C;
    auto tp = app::parseTemporalPolicy(kTemporalPolicyTokens[panel_.temporalIndex]);
    if (tp.has_value()) cfg.temporalPolicy = *tp;

    const InitialConditionType requestedMode = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
    cfg.initialConditions.type = fallbackRuntimeSupportedMode(requestedMode);

    cfg.initialConditions.terrain.terrainBaseFrequency   = panel_.terrainBaseFrequency;
    cfg.initialConditions.terrain.terrainDetailFrequency = panel_.terrainDetailFrequency;
    cfg.initialConditions.terrain.terrainWarpStrength    = panel_.terrainWarpStrength;
    cfg.initialConditions.terrain.terrainAmplitude       = panel_.terrainAmplitude;
    cfg.initialConditions.terrain.terrainRidgeMix        = panel_.terrainRidgeMix;
    cfg.initialConditions.terrain.terrainOctaves         = panel_.terrainOctaves;
    cfg.initialConditions.terrain.terrainLacunarity      = panel_.terrainLacunarity;
    cfg.initialConditions.terrain.terrainGain            = panel_.terrainGain;
    cfg.initialConditions.terrain.seaLevel               = panel_.seaLevel;
    cfg.initialConditions.terrain.polarCooling           = panel_.polarCooling;
    cfg.initialConditions.terrain.latitudeBanding        = panel_.latitudeBanding;
    cfg.initialConditions.terrain.humidityFromWater      = panel_.humidityFromWater;
    cfg.initialConditions.terrain.biomeNoiseStrength     = panel_.biomeNoiseStrength;
    cfg.initialConditions.terrain.islandDensity          = panel_.islandDensity;
    cfg.initialConditions.terrain.islandFalloff          = panel_.islandFalloff;
    cfg.initialConditions.terrain.coastlineSharpness     = panel_.coastlineSharpness;
    cfg.initialConditions.terrain.archipelagoJitter      = panel_.archipelagoJitter;
    cfg.initialConditions.terrain.erosionStrength        = panel_.erosionStrength;
    cfg.initialConditions.terrain.shelfDepth             = panel_.shelfDepth;

    cfg.initialConditions.conway.targetVariable = panel_.conwayTargetVariable;
    cfg.initialConditions.conway.aliveProbability = panel_.conwayAliveProbability;
    cfg.initialConditions.conway.aliveValue = panel_.conwayAliveValue;
    cfg.initialConditions.conway.deadValue = panel_.conwayDeadValue;
    cfg.initialConditions.conway.smoothingPasses = panel_.conwaySmoothingPasses;

    cfg.initialConditions.grayScott.targetVariableA = panel_.grayScottTargetVariableA;
    cfg.initialConditions.grayScott.targetVariableB = panel_.grayScottTargetVariableB;
    cfg.initialConditions.grayScott.backgroundA = panel_.grayScottBackgroundA;
    cfg.initialConditions.grayScott.backgroundB = panel_.grayScottBackgroundB;
    cfg.initialConditions.grayScott.spotValueA = panel_.grayScottSpotValueA;
    cfg.initialConditions.grayScott.spotValueB = panel_.grayScottSpotValueB;
    cfg.initialConditions.grayScott.spotCount = panel_.grayScottSpotCount;
    cfg.initialConditions.grayScott.spotRadius = panel_.grayScottSpotRadius;
    cfg.initialConditions.grayScott.spotJitter = panel_.grayScottSpotJitter;

    cfg.initialConditions.waves.targetVariable = panel_.wavesTargetVariable;
    cfg.initialConditions.waves.baseline = panel_.waveBaseline;
    cfg.initialConditions.waves.dropAmplitude = panel_.waveDropAmplitude;
    cfg.initialConditions.waves.dropRadius = panel_.waveDropRadius;
    cfg.initialConditions.waves.dropCount = panel_.waveDropCount;
    cfg.initialConditions.waves.dropJitter = panel_.waveDropJitter;
    cfg.initialConditions.waves.ringFrequency = panel_.waveRingFrequency;

    runtime_.setConfig(cfg);

    std::ostringstream out;
    out << "config_applied seed=" << cfg.seed
        << " grid=" << cfg.grid.width << 'x' << cfg.grid.height
        << " execution_profile=" << toString(cfg.tier)
        << " temporal=" << app::temporalPolicyToString(cfg.temporalPolicy);
    appendLog(out.str());
}

// Triggers overlay notification icon.
// @param icon Overlay icon to display
void triggerOverlay(const OverlayIcon icon) {
    overlay_.icon  = icon;
    overlay_.alpha = 1.0f;
}

// Pushes toast notification to display queue.
// @param level Severity level
// @param title Toast title
// @param message Toast message
// @param durationSec Display duration in seconds
void pushToast(const ToastLevel level, const std::string& title, const std::string& message, const float durationSec = 4.0f) {
    if (message.empty()) {
        return;
    }
    ToastItem toast;
    toast.level = level;
    toast.title = title;
    toast.message = message;
    toast.createdSec = glfwGetTime();
    toast.durationSec = std::clamp(durationSec, 1.0f, 10.0f);
    toasts_.push_back(std::move(toast));
    toastHistory_.push_back(toasts_.back());
    if (toasts_.size() > 8u) {
        toasts_.erase(toasts_.begin(), toasts_.begin() + static_cast<std::ptrdiff_t>(toasts_.size() - 8u));
    }
    if (toastHistory_.size() > 200u) {
        toastHistory_.erase(
            toastHistory_.begin(),
            toastHistory_.begin() + static_cast<std::ptrdiff_t>(toastHistory_.size() - 200u));
    }
}

// Draws toast notifications.
void drawToasts() {
    if (toasts_.empty()) {
        return;
    }

    const double now = glfwGetTime();
    toasts_.erase(
        std::remove_if(toasts_.begin(), toasts_.end(), [&](const ToastItem& toast) {
            return (now - toast.createdSec) >= static_cast<double>(toast.durationSec);
        }),
        toasts_.end());
    if (toasts_.empty()) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float cursorY = 16.0f;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - 16.0f, 16.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    if (!ImGui::Begin("##toast_host", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs)) {
        ImGui::End();
        return;
    }

    auto colorForLevel = [](const ToastLevel level) {
        switch (level) {
            case ToastLevel::Error: return ImVec4(0.95f, 0.35f, 0.35f, 0.95f);
            case ToastLevel::Warning: return ImVec4(0.95f, 0.75f, 0.35f, 0.95f);
            case ToastLevel::Success: return ImVec4(0.45f, 0.85f, 0.50f, 0.95f);
            default: return ImVec4(0.45f, 0.70f, 0.95f, 0.95f);
        }
    };

    for (const auto& toast : toasts_) {
        ImGui::SetCursorPosY(cursorY);
        const ImVec4 accent = colorForLevel(toast.level);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 24, 36, 238));
        ImGui::PushID(&toast);
        ImGui::BeginChild("toast_body", ImVec2(360.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
        ImGui::PopStyleColor();
        ImGui::TextColored(accent, "%s", toast.title.empty() ? "Notice" : toast.title.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f);
        ImGui::TextUnformatted(toast.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopID();
        cursorY += 70.0f;
    }

    ImGui::End();
}

// Enters simulation paused state.
void enterSimulationPaused() {
    viz_.autoRun = false;
    cancelPendingSimulationSteps();
    if (runtime_.isRunning() && !runtime_.isPaused()) {
        std::string msg;
        runtime_.pause(msg);
        if (!msg.empty()) appendLog(msg);
    }
    
    // Ensure field names are populated before visualizing
    refreshFieldNames();
    
    // Force an immediate snapshot capture for visualization
    RuntimeCheckpoint checkpoint;
    std::string msg;
    if (runtime_.captureCheckpoint(checkpoint, msg, false)) {
        viz_.cachedCheckpoint = std::move(checkpoint);
        viz_.hasCachedCheckpoint = true;
        viz_.snapshotDirty = false;
        viz_.lastSnapshotTimeSec = glfwGetTime();
    } else if (!msg.empty()) {
        appendLog("snapshot_capture_failed: " + msg);
    }
    
    requestSnapshotRefresh();
    appState_ = AppState::Simulation;
    triggerOverlay(OverlayIcon::Pause);
}

// Appends line to application log.
// @param line Log line to append
void appendLog(const std::string& line) {
    if (line.empty()) return;
    logs_.push_back(line);
    if (logs_.size() > 2000)
        logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 2000));

    const std::string lower = app::toLower(line);
    std::string title;
    std::string message;
    ToastLevel level = ToastLevel::Info;
    bool handled = false;

    if (lower.find("world_opened") != std::string::npos) {
        title = "World opened";
        message = (lower.find("source=checkpoint") != std::string::npos)
            ? "Opened from saved checkpoint."
            : "Opened from profile settings (no checkpoint found).";
        level = ToastLevel::Success;
        handled = true;
    } else if (lower.find("world_saved") != std::string::npos) {
        title = "World saved";
        message = "Profile and checkpoint were saved.";
        level = ToastLevel::Success;
        handled = true;
    } else if (lower.find("parameter_preset_saved") != std::string::npos) {
        title = "Preset saved";
        message = "Parameter preset saved successfully.";
        level = ToastLevel::Success;
        handled = true;
    } else if (lower.find("parameter_preset_save_failed") != std::string::npos) {
        title = "Preset save failed";
        message = "Could not save parameter preset. Check destination path and write permissions.";
        level = ToastLevel::Error;
        handled = true;
    } else if (lower.find("event_log_replay_failed reason=runtime_not_paused") != std::string::npos) {
        title = "Replay requires pause";
        message = "Pause the simulation before replaying event log entries.";
        level = ToastLevel::Warning;
        handled = true;
    } else if (lower.find("event_log_replay_failed reason=runtime_not_running") != std::string::npos) {
        title = "Replay unavailable";
        message = "Start a simulation first, then replay event log entries.";
        level = ToastLevel::Warning;
        handled = true;
    } else if (lower.find("event_log_replay_complete") != std::string::npos) {
        title = "Replay complete";
        message = "Event log replay finished. Review applied vs skipped counts in Diagnostics log.";
        level = ToastLevel::Success;
        handled = true;
    } else if (lower.find("world_open_failed") != std::string::npos) {
        title = "Open world failed";
        message = "Could not open the selected world. Check compatibility and storage files.";
        level = ToastLevel::Error;
        handled = true;
    } else if (lower.find("world_create_blocked") != std::string::npos) {
        title = "World creation blocked";
        message = "Preflight checks found blocking issues. Resolve required items before creating the world.";
        level = ToastLevel::Warning;
        handled = true;
    } else if (lower.find("wizard_step_blocked") != std::string::npos) {
        title = "Wizard step blocked";
        message = "Resolve required binding or preflight issues to continue.";
        level = ToastLevel::Warning;
        handled = true;
    } else if (lower.find("timeline_capture_skip reason=not_due") != std::string::npos ||
               lower.find("timeline_capture_skip reason=runtime_inactive") != std::string::npos) {
        handled = true;
    }

    if (handled) {
        if (!title.empty() && !message.empty()) {
            pushToast(level, title, message, 4.5f);
        }
        return;
    }

    const OperationResult typedResult = translateOperationResult(line);
    if (typedResult.status == OperationStatus::Failure) {
        pushToast(
            ToastLevel::Error,
            "Runtime error",
            typedResult.message.empty() ? line : typedResult.message,
            5.0f);
    } else if (typedResult.status == OperationStatus::Warning) {
        pushToast(
            ToastLevel::Warning,
            "Warning",
            typedResult.message.empty() ? line : typedResult.message,
            4.5f);
    }
}

[[nodiscard]] std::filesystem::path displayPrefsPathForWorld(const std::string& w) const {
    if (w.empty()) return {};
    return std::filesystem::path("checkpoints") / "worlds" / (w + ".displayprefs");
}
[[nodiscard]] std::filesystem::path activeDisplayPrefsPath() const {
    return displayPrefsPathForWorld(runtime_.activeWorldName());
}

// Opens selected world from model selector.
void openSelectedWorld() {
    appendLog("open_world_button_clicked");
    if (sessionUi_.selectedWorldIndex < 0 ||
        sessionUi_.selectedWorldIndex >= static_cast<int>(sessionUi_.worlds.size())) {
        appendLog("open_world_error selected_index_invalid");
        return;
    }
    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
    beginOperationStatus("open world", 0.15f, world.worldName.c_str());
    const auto startedAt = std::chrono::steady_clock::now();
    std::string msg;
    if (runtime_.openWorld(world.worldName, msg)) {
        appendLog(msg);
        syncPanelFromConfig();
        refreshFieldNames();
        resetDisplayConfigToDefaults();
        loadDisplayPrefs();
        completeOperationStatus(startedAt, "world opened");
        if (world.hasCheckpoint) {
            setSessionStatusText(
                std::string("Opened '") + world.worldName +
                "' from its saved checkpoint" +
                (world.stepIndex > 0 ? std::string(" at step ") + std::to_string(world.stepIndex) + "." : std::string(".")));
        } else {
            setSessionStatusText(
                std::string("Opened '") + world.worldName +
                "' from profile settings because no checkpoint was available.");
        }
        enterSimulationPaused();
    } else {
        appendLog(msg);
        completeOperationStatus(startedAt, "world open failed");
        setSessionStatusText(translateSessionStatusMessage(msg));
    }
}

// Resets display configuration to default values.
void resetDisplayConfigToDefaults() {
    const VisualizationState defaults{};
    viz_.layout                      = defaults.layout;
    viz_.viewports                   = defaults.viewports;
    viz_.activeViewportEditor        = defaults.activeViewportEditor;
    viz_.displayRefreshEveryNSteps   = defaults.displayRefreshEveryNSteps;
    viz_.displayTargetRefreshHz      = defaults.displayTargetRefreshHz;
    viz_.displayRefreshOnStateChange = defaults.displayRefreshOnStateChange;
    viz_.unlimitedSimSpeed           = defaults.unlimitedSimSpeed;
    viz_.generationPreviewDisplayType = defaults.generationPreviewDisplayType;
    viewportManager_.resize(viz_.viewports.size());
    viewportManager_.setSyncPan(false);
    viewportManager_.setSyncZoom(false);
    for (std::size_t i = 0; i < viz_.viewports.size(); ++i) {
        viewportManager_.fit(i);
    }
    viewportRenderRules_.assign(viz_.viewports.size(), {});
    ensureViewportStateConsistency();
    requestViewportEditorSelection(static_cast<std::size_t>(viz_.activeViewportEditor));
}

// Saves display preferences to file.
void saveDisplayPrefs() {
    ensureViewportStateConsistency();

    const auto path = activeDisplayPrefsPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;

    out << "layout="                    << static_cast<int>(viz_.layout)                        << "\n";
    out << "generationPreviewDisplayType=" << static_cast<int>(viz_.generationPreviewDisplayType) << "\n";
    out << "displayRefreshEveryNSteps=" << viz_.displayRefreshEveryNSteps                       << "\n";
    out << "displayTargetRefreshHz="   << viz_.displayTargetRefreshHz                          << "\n";
    out << "displayRefreshOnStateChange=" << static_cast<int>(viz_.displayRefreshOnStateChange) << "\n";
    out << "unlimitedSimSpeed="         << static_cast<int>(viz_.unlimitedSimSpeed)             << "\n";
    out << "viewportCount="             << viz_.viewports.size()                                 << "\n";
    out << "syncPan="                   << static_cast<int>(viewportManager_.syncPan())         << "\n";
    out << "syncZoom="                  << static_cast<int>(viewportManager_.syncZoom())        << "\n";

    for (std::size_t i = 0; i < viz_.viewports.size(); ++i) {
        const auto& vp = viz_.viewports[i];
        const auto& cam = viewportManager_.camera(i);
        out << "vp" << i << "_primaryFieldIndex="  << vp.primaryFieldIndex             << "\n";
        out << "vp" << i << "_displayType="        << static_cast<int>(vp.displayType)  << "\n";
        out << "vp" << i << "_renderMode="         << static_cast<int>(vp.renderMode)   << "\n";
        out << "vp" << i << "_normalizationMode="  << static_cast<int>(vp.normalizationMode) << "\n";
        out << "vp" << i << "_colorMapMode="       << static_cast<int>(vp.colorMapMode)  << "\n";
        out << "vp" << i << "_heatmapNormalization=" << static_cast<int>(vp.heatmapNormalization) << "\n";
        out << "vp" << i << "_heatmapColorMap="    << static_cast<int>(vp.heatmapColorMap) << "\n";
        out << "vp" << i << "_heatmapPowerExponent=" << vp.heatmapPowerExponent << "\n";
        out << "vp" << i << "_heatmapQuantileLow=" << vp.heatmapQuantileLow << "\n";
        out << "vp" << i << "_heatmapQuantileHigh=" << vp.heatmapQuantileHigh << "\n";
        out << "vp" << i << "_showSparseOverlay=" << static_cast<int>(vp.showSparseOverlay) << "\n";
        out << "vp" << i << "_displayAutoWaterLevel=" << static_cast<int>(vp.displayManager.autoWaterLevel) << "\n";
        out << "vp" << i << "_displayWaterThreshold=" << vp.displayManager.waterLevel << "\n";
        out << "vp" << i << "_displayWaterQuantile=" << vp.displayManager.autoWaterQuantile << "\n";
        out << "vp" << i << "_displayLowlandThreshold=" << vp.displayManager.lowlandThreshold << "\n";
        out << "vp" << i << "_displayHighlandThreshold=" << vp.displayManager.highlandThreshold << "\n";
        out << "vp" << i << "_displayWaterPresenceThreshold=" << vp.displayManager.waterPresenceThreshold << "\n";
        out << "vp" << i << "_displayShallowWaterDepth=" << vp.displayManager.shallowWaterDepth << "\n";
        out << "vp" << i << "_displayHighMoistureThreshold=" << vp.displayManager.highMoistureThreshold << "\n";
        out << "vp" << i << "_showWindMagnitudeBackground=" << vp.showWindMagnitudeBackground << "\n";
        out << "vp" << i << "_showVectorField="    << vp.showVectorField                << "\n";
        out << "vp" << i << "_vectorXFieldIndex="  << vp.vectorXFieldIndex              << "\n";
        out << "vp" << i << "_vectorYFieldIndex="  << vp.vectorYFieldIndex              << "\n";
        out << "vp" << i << "_vectorStride="       << vp.vectorStride                   << "\n";
        out << "vp" << i << "_vectorScale="        << vp.vectorScale                    << "\n";
        out << "vp" << i << "_showContours="       << vp.showContours                   << "\n";
        out << "vp" << i << "_contourInterval="    << vp.contourInterval                << "\n";
        out << "vp" << i << "_contourMaxLevels="   << vp.contourMaxLevels               << "\n";
        out << "vp" << i << "_customRuleEnabled="  << vp.customRuleEnabled              << "\n";
        out << "vp" << i << "_showLegend="         << vp.showLegend                    << "\n";
        out << "vp" << i << "_fixedRangeMin="      << vp.fixedRangeMin                 << "\n";
        out << "vp" << i << "_fixedRangeMax="      << vp.fixedRangeMax                 << "\n";
        out << "vp" << i << "_brightness="         << vp.brightness                    << "\n";
        out << "vp" << i << "_contrast="           << vp.contrast                      << "\n";
        out << "vp" << i << "_gamma="              << vp.gamma                         << "\n";
        out << "vp" << i << "_invertColors="       << static_cast<int>(vp.invertColors) << "\n";
        out << "vp" << i << "_showGrid="           << static_cast<int>(vp.showGrid) << "\n";
        out << "vp" << i << "_gridOpacity="        << vp.gridOpacity << "\n";
        out << "vp" << i << "_gridLineThickness="  << vp.gridLineThickness << "\n";
        out << "vp" << i << "_showBoundary="       << static_cast<int>(vp.showBoundary) << "\n";
        out << "vp" << i << "_boundaryOpacity="    << vp.boundaryOpacity << "\n";
        out << "vp" << i << "_boundaryThickness="  << vp.boundaryThickness << "\n";
        out << "vp" << i << "_zoom="               << cam.zoom                         << "\n";
        out << "vp" << i << "_panX="               << cam.panX                         << "\n";
        out << "vp" << i << "_panY="               << cam.panY                         << "\n";
    }
}

// Loads display preferences from file.
void loadDisplayPrefs() {
    ensureViewportStateConsistency();

    const auto path = activeDisplayPrefsPath();
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        try {
            if (key == "layout")                       viz_.layout = static_cast<ScreenLayout>(std::stoi(val));
            else if (key == "generationPreviewDisplayType") viz_.generationPreviewDisplayType = static_cast<DisplayType>(std::stoi(val));
            // Legacy global display keys: apply to all views for backward compatibility.
            else if (key == "displayAutoWaterLevel") {
                const bool enabled = (std::stoi(val) != 0);
                for (auto& view : viz_.viewports) {
                    view.displayManager.autoWaterLevel = enabled;
                }
            }
            else if (key == "displayWaterThreshold") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.waterLevel = v;
                }
            }
            else if (key == "displayWaterQuantile") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.autoWaterQuantile = v;
                }
            }
            else if (key == "displayLowlandThreshold") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.lowlandThreshold = v;
                }
            }
            else if (key == "displayHighlandThreshold") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.highlandThreshold = v;
                }
            }
            else if (key == "displayWaterPresenceThreshold") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.waterPresenceThreshold = v;
                }
            }
            else if (key == "displayShallowWaterDepth") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.shallowWaterDepth = v;
                }
            }
            else if (key == "displayHighMoistureThreshold") {
                const float v = std::stof(val);
                for (auto& view : viz_.viewports) {
                    view.displayManager.highMoistureThreshold = v;
                }
            }
            else if (key == "displayRefreshEveryNSteps") viz_.displayRefreshEveryNSteps = std::stoi(val);
            else if (key == "displayTargetRefreshHz")   viz_.displayTargetRefreshHz = std::stoi(val);
            else if (key == "displayRefreshOnStateChange") viz_.displayRefreshOnStateChange = (std::stoi(val) != 0);
            else if (key == "unlimitedSimSpeed")       viz_.unlimitedSimSpeed = (std::stoi(val) != 0);
            else if (key == "viewportCount") {
                const std::size_t requestedCount = clampedViewportCount(static_cast<std::size_t>(std::max(1, std::stoi(val))));
                if (requestedCount < viz_.viewports.size()) {
                    viz_.viewports.resize(requestedCount);
                } else if (requestedCount > viz_.viewports.size()) {
                    const std::size_t previousSize = viz_.viewports.size();
                    viz_.viewports.reserve(requestedCount);
                    for (std::size_t i = previousSize; i < requestedCount; ++i) {
                        viz_.viewports.push_back(VisualizationState::makeDefaultViewportConfig(i));
                    }
                }
                ensureViewportStateConsistency();
            }
            else if (key == "syncPan") viewportManager_.setSyncPan(std::stoi(val) != 0);
            else if (key == "syncZoom") viewportManager_.setSyncZoom(std::stoi(val) != 0);
            else if (key.rfind("vp", 0) == 0) {
                const std::size_t underscorePos = key.find('_', 2);
                if (underscorePos == std::string::npos) {
                    continue;
                }
                const std::string indexText = key.substr(2, underscorePos - 2);
                if (indexText.empty()) {
                    continue;
                }
                const int vi = std::stoi(indexText);
                if (vi < 0) {
                    continue;
                }
                const std::size_t viewportIndex = static_cast<std::size_t>(vi);
                if (viewportIndex >= viz_.viewports.size()) {
                    continue;
                }
                auto& vp = viz_.viewports[viewportIndex];
                const std::string sub = key.substr(underscorePos + 1);
                if      (sub == "primaryFieldIndex")  vp.primaryFieldIndex  = std::stoi(val);
                else if (sub == "displayType")        vp.displayType        = static_cast<DisplayType>(std::stoi(val));
                else if (sub == "renderMode")         vp.renderMode         = static_cast<ViewportRenderMode>(std::stoi(val));
                else if (sub == "normalizationMode")  vp.normalizationMode  = static_cast<NormalizationMode>(std::stoi(val));
                else if (sub == "colorMapMode")       vp.colorMapMode       = static_cast<ColorMapMode>(std::stoi(val));
                else if (sub == "heatmapNormalization") vp.heatmapNormalization = static_cast<HeatmapNormalization>(std::stoi(val));
                else if (sub == "heatmapColorMap")    vp.heatmapColorMap    = static_cast<HeatmapColorMap>(std::stoi(val));
                else if (sub == "heatmapPowerExponent") vp.heatmapPowerExponent = std::stof(val);
                else if (sub == "heatmapQuantileLow") vp.heatmapQuantileLow = std::stof(val);
                else if (sub == "heatmapQuantileHigh") vp.heatmapQuantileHigh = std::stof(val);
                else if (sub == "showSparseOverlay") vp.showSparseOverlay = (std::stoi(val) != 0);
                else if (sub == "displayAutoWaterLevel") vp.displayManager.autoWaterLevel = (std::stoi(val) != 0);
                else if (sub == "displayWaterThreshold") vp.displayManager.waterLevel = std::stof(val);
                else if (sub == "displayWaterQuantile") vp.displayManager.autoWaterQuantile = std::stof(val);
                else if (sub == "displayLowlandThreshold") vp.displayManager.lowlandThreshold = std::stof(val);
                else if (sub == "displayHighlandThreshold") vp.displayManager.highlandThreshold = std::stof(val);
                else if (sub == "displayWaterPresenceThreshold") vp.displayManager.waterPresenceThreshold = std::stof(val);
                else if (sub == "displayShallowWaterDepth") vp.displayManager.shallowWaterDepth = std::stof(val);
                else if (sub == "displayHighMoistureThreshold") vp.displayManager.highMoistureThreshold = std::stof(val);
                else if (sub == "showWindMagnitudeBackground") vp.showWindMagnitudeBackground = (std::stoi(val) != 0);
                else if (sub == "showVectorField")    vp.showVectorField    = (std::stoi(val) != 0);
                else if (sub == "vectorXFieldIndex")  vp.vectorXFieldIndex  = std::stoi(val);
                else if (sub == "vectorYFieldIndex")  vp.vectorYFieldIndex  = std::stoi(val);
                else if (sub == "vectorStride")       vp.vectorStride       = std::stoi(val);
                else if (sub == "vectorScale")        vp.vectorScale        = std::stof(val);
                else if (sub == "showContours")       vp.showContours       = (std::stoi(val) != 0);
                else if (sub == "contourInterval")    vp.contourInterval    = std::stof(val);
                else if (sub == "contourMaxLevels")   vp.contourMaxLevels   = std::stoi(val);
                else if (sub == "customRuleEnabled")  vp.customRuleEnabled  = (std::stoi(val) != 0);
                else if (sub == "showLegend")         vp.showLegend         = (std::stoi(val) != 0);
                else if (sub == "fixedRangeMin")      vp.fixedRangeMin      = std::stof(val);
                else if (sub == "fixedRangeMax")      vp.fixedRangeMax      = std::stof(val);
                else if (sub == "brightness")         vp.brightness         = std::stof(val);
                else if (sub == "contrast")           vp.contrast           = std::stof(val);
                else if (sub == "gamma")              vp.gamma              = std::stof(val);
                else if (sub == "invertColors")       vp.invertColors       = (std::stoi(val) != 0);
                else if (sub == "showGrid")           vp.showGrid           = (std::stoi(val) != 0);
                else if (sub == "gridOpacity")        vp.gridOpacity        = std::stof(val);
                else if (sub == "gridLineThickness")  vp.gridLineThickness  = std::stof(val);
                else if (sub == "showBoundary")       vp.showBoundary       = (std::stoi(val) != 0);
                else if (sub == "boundaryOpacity")    vp.boundaryOpacity    = std::stof(val);
                else if (sub == "boundaryThickness")  vp.boundaryThickness  = std::stof(val);
                else if (sub == "zoom") {
                    viewportManager_.setZoom(viewportIndex, std::stof(val));
                } else if (sub == "panX") {
                    const auto c = viewportManager_.camera(viewportIndex);
                    viewportManager_.setPan(viewportIndex, std::stof(val), c.panY);
                } else if (sub == "panY") {
                    const auto c = viewportManager_.camera(viewportIndex);
                    viewportManager_.setPan(viewportIndex, c.panX, std::stof(val));
                }
            }
        } catch (...) {}
    }
    // clamp after load
    for (auto& vp : viz_.viewports) {
        vp.displayManager.waterLevel = std::clamp(vp.displayManager.waterLevel, 0.0f, 1.0f);
        vp.displayManager.autoWaterQuantile = std::clamp(vp.displayManager.autoWaterQuantile, 0.0f, 1.0f);
        vp.displayManager.lowlandThreshold  = std::clamp(vp.displayManager.lowlandThreshold,  0.0f, 1.0f);
        vp.displayManager.highlandThreshold = std::clamp(
            vp.displayManager.highlandThreshold,
            vp.displayManager.lowlandThreshold + 0.01f, 1.0f);
        vp.displayManager.waterPresenceThreshold = std::clamp(
            vp.displayManager.waterPresenceThreshold, 0.0f, 1.0f);
        vp.displayManager.shallowWaterDepth = std::clamp(
            vp.displayManager.shallowWaterDepth, 0.0f, 0.5f);
        vp.displayManager.highMoistureThreshold = std::clamp(
            vp.displayManager.highMoistureThreshold, 0.0f, 1.0f);
        vp.brightness = std::clamp(vp.brightness, 0.1f, 3.0f);
        vp.contrast = std::clamp(vp.contrast, 0.1f, 3.0f);
        vp.gamma = std::clamp(vp.gamma, 0.2f, 3.0f);
        vp.gridOpacity = std::clamp(vp.gridOpacity, 0.0f, 1.0f);
        vp.gridLineThickness = std::clamp(vp.gridLineThickness, 0.5f, 3.0f);
        vp.boundaryOpacity = std::clamp(vp.boundaryOpacity, 0.0f, 1.0f);
        vp.boundaryThickness = std::clamp(vp.boundaryThickness, 0.5f, 6.0f);
    }
    viz_.displayRefreshEveryNSteps = std::clamp(viz_.displayRefreshEveryNSteps, 1, 1000);
    viz_.displayTargetRefreshHz = std::clamp(viz_.displayTargetRefreshHz, 15, 240);
    ensureViewportStateConsistency();
    requestViewportEditorSelection(static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, static_cast<int>(viz_.viewports.size()) - 1)));
    clampVisualizationIndices();
}

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
