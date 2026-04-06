#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

// Keyboard shortcut handling - called once per frame before ImGui::NewFrame
void handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    // Space -> toggle pause / resume
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (runtime_.isRunning()) {
            if (runtime_.isPaused()) {
                std::string msg;
                if (runtime_.resume(msg)) { viz_.autoRun = true; appendLog(msg); triggerOverlay(OverlayIcon::Play); }
            } else {
                cancelPendingSimulationSteps();
                std::string msg;
                if (runtime_.pause(msg)) { viz_.autoRun = false; appendLog(msg); triggerOverlay(OverlayIcon::Pause); }
            }
        }
    }

    // Ctrl+S -> save active world
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (!runtime_.activeWorldName().empty()) {
            std::string msg;
            if (runtime_.saveActiveWorld(msg)) appendLog(msg);
            else appendLog(msg);
        }
    }

    // Right arrow -> single step (when paused)
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && runtime_.isRunning() && runtime_.isPaused()) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), msg);
        appendLog(msg);
        requestSnapshotRefresh();
    }

    ensureViewportStateConsistency();

    // F1-F12 -> switch active viewport editor (when available)
    static constexpr std::array<ImGuiKey, 12> kViewportHotkeys = {
        ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4,
        ImGuiKey_F5, ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8,
        ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12};
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

// Main control panel window
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

    ImGui::End();
}

// Status header - always visible, always up-to-date
void drawStatusHeader() {
    const bool running  = runtime_.isRunning();
    const bool paused   = runtime_.isPaused();
    const bool hasWorld = !runtime_.activeWorldName().empty();

    // top action bar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 16, 28, 255));
    ImGui::BeginChild("StatusHdr", ImVec2(0.0f, 82.0f), true);

    // Save & Return button
    {
        const char* label = hasWorld ? "Save & Exit  [Ctrl+S]" : "Back to Models";
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(40, 80, 140, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 105, 175, 240));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(30, 65, 115, 255));
        if (ImGui::Button(label, ImVec2(160.0f, 30.0f))) {
            viz_.autoRun = false;
            cancelPendingSimulationSteps();
            if (hasWorld && running) {
                std::string saveMsg;
                bool saved = runtime_.saveActiveWorld(saveMsg);
                appendLog(saveMsg);
            }
            std::string stopMsg;
            runtime_.stop(stopMsg);
            if (!stopMsg.empty()) appendLog(stopMsg);
            appState_ = AppState::ModelSelector;
            modelSelector_.open();
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
                appendLog(msg); requestSnapshotRefresh(); triggerOverlay(OverlayIcon::Play);
            } else {
                cancelPendingSimulationSteps();
                std::string msg; runtime_.pause(msg); viz_.autoRun = false;
                appendLog(msg); triggerOverlay(OverlayIcon::Pause);
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

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

//
// Tab: Simulation
//
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

void drawInterventionsTab() {
    ImGui::BeginChild("InterventionsTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

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

void drawParameterControlSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to edit runtime parameters.");
        return;
    }

    if (!runtime_.isPaused()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Parameter edits and manual patches require pause mode.");
    }

    std::vector<ParameterControl> controls;
    std::string msg;
    runtime_.parameterControls(controls, msg);

    if (!controls.empty()) {
        panel_.selectedParameterIndex = std::clamp(panel_.selectedParameterIndex, 0, static_cast<int>(controls.size() - 1));
        const auto& selected = controls[static_cast<std::size_t>(panel_.selectedParameterIndex)];

        if (ImGui::BeginCombo("Writable parameter##phase6", selected.name.c_str())) {
            for (int i = 0; i < static_cast<int>(controls.size()); ++i) {
                const bool selectedItem = (i == panel_.selectedParameterIndex);
                if (ImGui::Selectable(controls[static_cast<std::size_t>(i)].name.c_str(), selectedItem)) {
                    panel_.selectedParameterIndex = i;
                    panel_.parameterValue = controls[static_cast<std::size_t>(i)].value;
                }
            }
            ImGui::EndCombo();
        }

        const auto& active = controls[static_cast<std::size_t>(panel_.selectedParameterIndex)];
        sliderFloatWithHint("Parameter value", &panel_.parameterValue, active.minValue, active.maxValue, "%.5f",
            "Applies a deterministic global forcing update by queueing input patches for the target field.");
        ImGui::TextDisabled("Target field: %s  | units: %s", active.targetVariable.c_str(), active.units.c_str());

        if (PrimaryButton("Apply parameter", ImVec2(-1.0f, 26.0f))) {
            std::string setMsg;
            runtime_.setParameterValue(active.name, panel_.parameterValue, "ui_parameter_update", setMsg);
            appendLog(setMsg);
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
        runtime_.applyManualPatch(
            panel_.manualPatchVariable,
            cell,
            panel_.manualPatchValue,
            panel_.manualPatchNote,
            patchMsg);
        appendLog(patchMsg);
    }

    if (SecondaryButton("Undo last manual edit", ImVec2(-1.0f, 24.0f))) {
        std::string undoMsg;
        runtime_.undoLastManualPatch(undoMsg);
        appendLog(undoMsg);
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
        appendLog(presetMsg);
    }
    ImGui::SameLine();
    if (SecondaryButton("Load preset", ImVec2(140.0f, 24.0f))) {
        ParameterPreset preset;
        std::string presetMsg;
        if (loadParameterPreset(presetPath, preset, presetMsg)) {
            appendLog(presetMsg);
            for (const auto& parameter : preset.parameters) {
                std::string setMsg;
                runtime_.setParameterValue(parameter.name, parameter.value, "preset_load", setMsg);
                appendLog(setMsg);
            }
        } else {
            appendLog(presetMsg);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual event log");
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
        appendLog(eventMsg);
    }
}

void drawPerturbationSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to queue perturbations.");
        return;
    }

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
        appendLog(applyMsg);
    }
}

void drawTierSelector() {
    const bool isRunning = runtime_.isRunning();

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
    ImGui::Spacing();

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
                appendLog(msg);
                requestSnapshotRefresh();
                enterSimulationPaused();
            } else {
                appendLog(msg);
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Restarts the simulation with updated profile and temporal behavior settings.\nThe simulation will be paused after restart.");
    } else {
        if (PrimaryButton("Apply these settings", ImVec2(-1.0f, 30.0f))) {
            applyConfigFromPanel();
            appendLog("Execution profile and temporal behavior settings applied. Use 'Start Simulation' to begin.");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Applies profile/temporal settings without starting the simulation.\nUse 'Start Simulation' button to begin.");
    }
}

void drawPlaybackSection() {
    const bool running = runtime_.isRunning();
    const bool paused  = runtime_.isPaused();

    if (!running) {
        if (PrimaryButton("Start Simulation", ImVec2(-1.0f, 34.0f))) {
            applyConfigFromPanel();
            std::string msg;
            if (runtime_.start(msg)) {
                viz_.autoRun = true; appendLog(msg);
                refreshFieldNames(); requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Play);
            } else appendLog(msg);
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
                appendLog(msg); requestSnapshotRefresh(); triggerOverlay(OverlayIcon::Play);
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(130, 100, 20, 220));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(160, 125, 30, 240));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(100, 80, 15, 255));
            if (ImGui::Button("Pause [Space]", ImVec2(halfW, 34.0f))) {
                cancelPendingSimulationSteps();
                std::string msg; runtime_.pause(msg); viz_.autoRun = false;
                appendLog(msg); triggerOverlay(OverlayIcon::Pause);
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        // Stop
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(120, 40, 40, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(155, 55, 55, 240));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(95, 30, 30, 255));
        if (ImGui::Button("Stop & Reset", ImVec2(-1.0f, 34.0f))) {
            viz_.autoRun = false;
            cancelPendingSimulationSteps();
            std::string msg; runtime_.stop(msg); appendLog(msg);
            requestSnapshotRefresh(); triggerOverlay(OverlayIcon::Pause);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Stop and terminate the runtime.\nThe world state is NOT automatically saved here - use Save & Exit or Ctrl+S.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Auto-run speed controls (only shown when playing)
        if (!paused && viz_.autoRun) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Continuous auto-run");
            checkboxWithHint("Unlimited simulation speed", &viz_.unlimitedSimSpeed,
                "Run the simulation thread at full speed.\n"
                "Disable this to insert a small yield between batches and keep the UI more relaxed.");
            sliderIntWithHint("Steps between display updates", &viz_.displayRefreshEveryNSteps, 1, 1000,
                "Refresh the display after every N simulation steps.\n"
                "1 = update every step. Higher values favor simulation throughput over visual refresh rate.\n"
                "A live latency cap still keeps the viewport refreshing regularly during fast auto-run.");
            const float estStepsPerSec = estimatedSimulationStepsPerSecond();
            const float estRefreshesPerSec = estimatedDisplayRefreshesPerSecond();
            if (estStepsPerSec > 0.0f) {
                ImGui::TextDisabled("Estimated: ~%.0f sim steps/sec, ~%.1f display updates/sec (<= %.0f ms latency)",
                    estStepsPerSec, estRefreshesPerSec, displayRefreshLatencyCapMs());
            } else {
                ImGui::TextDisabled("Estimated: waiting for a completed auto-run batch...");
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Manual / Paused");
            ImGui::TextDisabled("Use Step Controls below, or press Space to resume.");
        }
    }
}

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
        appendLog(msg); requestSnapshotRefresh();
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
            appendLog(msg); requestSnapshotRefresh();
        }
    }
}

void drawTimeControlSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to use time controls.");
        return;
    }

    sliderFloatWithHint("Playback speed", &panel_.playbackSpeed, 0.1f, 8.0f, "%.2fx",
        "Logical playback multiplier used by time control tools.");
    if (PrimaryButton("Apply speed", ImVec2(140.0f, 24.0f))) {
        std::string msg;
        runtime_.setPlaybackSpeed(panel_.playbackSpeed, msg);
        appendLog(msg);
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
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.seekStep(panel_.seekTargetStep, msg);
        appendLog(msg);
        requestSnapshotRefresh();
    }
    ImGui::SameLine();
    NumericSliderPairInt("Step backward", &panel_.backwardStepCount, 1, 100000, "%d", 55.0f);
    if (SecondaryButton("Apply backward step", ImVec2(-1.0f, 26.0f))) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.stepBackward(static_cast<std::uint32_t>(std::max(1, panel_.backwardStepCount)), msg);
        appendLog(msg);
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
        appendLog(msg);
    }

    TimeControlStatus status;
    status.currentStep = currentStep;
    status.targetStep = panel_.seekTargetStep;
    status.simulationTime = static_cast<float>(currentStep);
    status.playbackSpeed = runtime_.playbackSpeed();
    ImGui::TextDisabled("%s", formatTimeControlStatus(status).c_str());

    const float progress = timeControlProgress(status);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
}

void drawCheckpointSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Start the simulation to use checkpoints.");
        return;
    }

    inputTextWithHint("Label##cp", panel_.checkpointLabel, sizeof(panel_.checkpointLabel),
        "Identifier used to save/restore this checkpoint.\nAny alphanumeric string is valid.");

    const float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (PrimaryButton("Store##cp", ImVec2(btnW, 26.0f))) {
        std::string msg;
        runtime_.createCheckpoint(panel_.checkpointLabel, msg);
        appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Save the current state under this label (in-memory only).");
    ImGui::SameLine(0,4);
    if (PrimaryButton("Restore##cp", ImVec2(btnW, 26.0f))) {
        cancelPendingSimulationSteps();
        std::string msg;
        runtime_.restoreCheckpoint(panel_.checkpointLabel, msg);
        appendLog(msg); requestSnapshotRefresh();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Roll back to the saved checkpoint with this label.\nAll unsaved progress after that point is discarded.");
    ImGui::SameLine(0,4);
    if (SecondaryButton("List##cp", ImVec2(-1.0f, 26.0f))) {
        std::string msg;
        runtime_.listCheckpoints(msg);
        appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("List all stored in-memory checkpoint labels.");

    ImGui::Spacing();
    ImGui::TextDisabled("In-memory checkpoints are lost when the simulation stops.\nUse Save & Exit to persist to disk.");
}

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
void drawDisplayTab() {
    ImGui::BeginChild("DispTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

    PushSectionTint(5);
    if (ImGui::CollapsingHeader("Layout & Viewports", ImGuiTreeNodeFlags_DefaultOpen))
        drawLayoutSection();
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(6);
    if (ImGui::CollapsingHeader("Viewport Settings", ImGuiTreeNodeFlags_DefaultOpen))
        drawViewportSettingsSection();
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(7);
    if (ImGui::CollapsingHeader("Global Overlays"))
        drawOverlaySection();
    PopSectionTint();
    ImGui::Spacing();

    PushSectionTint(8);
    if (ImGui::CollapsingHeader("Camera & Optics"))
        drawOpticsSection();
    PopSectionTint();

    ImGui::EndChild();
}

void drawLayoutSection() {
    static constexpr const char* layoutNames[] = {
        "Single view", "Split Left/Right", "Split Top/Bottom", "4-Way Quad"};
    int layout = static_cast<int>(viz_.layout);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##layout", &layout, layoutNames,
            static_cast<int>(std::size(layoutNames)))) {
        viz_.layout = static_cast<ScreenLayout>(
            std::clamp(layout, 0, (int)std::size(layoutNames)-1));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Layout preset for initial viewport arrangement.\nUse Viewport Settings tabs to add/remove views (F1-F12 select tab-matched views).");

    ImGui::SameLine(0,6);
    if (SecondaryButton("Refresh fields", ImVec2(120.0f, 24.0f))) refreshFieldNames();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Re-query the runtime for the current list of named fields.");
    ImGui::SameLine(0,4);
    if (SecondaryButton("Save prefs", ImVec2(90.0f, 24.0f))) saveDisplayPrefs();

    // Generation preview display type
    ImGui::Spacing();
    ImGui::TextDisabled("World generation preview style:");
    static constexpr const char* dispTypeNames[] = {
        "Scalar Field", "Surface Category", "Relative Elevation", "Surface Water", "Moisture Map"};
    int previewMode = static_cast<int>(viz_.generationPreviewDisplayType);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##prevtype", &previewMode, dispTypeNames,
            static_cast<int>(std::size(dispTypeNames)))) {
        viz_.generationPreviewDisplayType = static_cast<DisplayType>(
            std::clamp(previewMode, 0, (int)std::size(dispTypeNames)-1));
    }

    // Water-level controls
    ImGui::Spacing();
    ImGui::Separator();
    checkboxWithHint("Auto water level", &viz_.displayManager.autoWaterLevel,
        "Derive the water/land threshold from the terrain elevation distribution\n"
        "at the configured quantile, rather than a fixed value.");
    if (viz_.displayManager.autoWaterLevel) {
        sliderFloatWithHint("Auto quantile", &viz_.displayManager.autoWaterQuantile,
            0.05f, 0.95f, "%.3f",
            "Terrain percentile used to place the waterline.\n"
            "0.50 = median, 0.40 = 40th percentile (more ocean).");
    } else {
        sliderFloatWithHint("Manual water level", &viz_.displayManager.waterLevel,
            0.0f, 1.0f, "%.3f",
            "Fixed elevation threshold that separates water cells from land.\n"
            "Cells below this value are rendered as water.");
    }
    sliderFloatWithHint("Lowland breakpoint", &viz_.displayManager.lowlandThreshold,
        0.0f, 1.0f, "%.3f",
        "Elevation below which land is classified as 'lowland/beach'.");
    viz_.displayManager.highlandThreshold = std::max(
        viz_.displayManager.highlandThreshold,
        viz_.displayManager.lowlandThreshold + 0.01f);
    sliderFloatWithHint("Highland breakpoint", &viz_.displayManager.highlandThreshold,
        0.0f, 1.0f, "%.3f",
        "Elevation above which land is classified as 'highland/mountain'.");
    sliderFloatWithHint("Shallow water depth", &viz_.displayManager.shallowWaterDepth,
        0.0f, 0.20f, "%.3f",
        "Depth threshold used to split shallow coastal water from deeper ocean water.");
    sliderFloatWithHint("Surface wetness threshold",
        &viz_.displayManager.waterPresenceThreshold, 0.0f, 0.5f, "%.3f",
        "Normalized surface-water threshold before above-water land is treated as wet shoreline.\n"
        "Lower = more cells rendered as wet coastal terrain.");
    sliderFloatWithHint("High humidity threshold", &viz_.displayManager.highMoistureThreshold,
        0.0f, 1.0f, "%.3f",
        "Humidity above which land is highlighted as wet in Surface Category mode.");
}

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
            if (i < 12u) {
                label += " [F" + std::to_string(i + 1u) + "]";
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

    int renderMode = static_cast<int>(vp.renderMode);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Render mode##rm", &renderMode, renderModeNames,
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
    int dt = static_cast<int>(vp.displayType);
    ImGui::SetNextItemWidth(-80.0f);
    if (ImGui::Combo("Type##dt", &dt, dispTypeNames,
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

    // Color map
    const bool windFieldMode = (vp.displayType == DisplayType::WindField);
    if (!windFieldMode) {
        int cm = static_cast<int>(vp.colorMapMode);
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::Combo("Palette##cm", &cm, colorMapNames,
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
        int nm = static_cast<int>(vp.normalizationMode);
        ImGui::SetNextItemWidth(-80.0f);
        if (ImGui::Combo("Range##nm", &nm, normNames,
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
    int hNorm = static_cast<int>(vp.heatmapNormalization);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Normalization##hmnorm", &hNorm, heatmapNormNames,
            static_cast<int>(std::size(heatmapNormNames)))) {
        vp.heatmapNormalization = static_cast<HeatmapNormalization>(std::clamp(hNorm, 0, static_cast<int>(std::size(heatmapNormNames) - 1)));
    }
    int hPalette = static_cast<int>(vp.heatmapColorMap);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Palette##hmpalette", &hPalette, heatmapPaletteNames,
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

void drawOverlaySection() {
    checkboxWithHint("Domain boundary", &visuals_.showBoundary,
        "Draw a border rectangle around the simulation domain.");
    if (visuals_.showBoundary) {
        ImGui::Indent();
        sliderFloatWithHint("Opacity##bo", &visuals_.boundaryOpacity, 0.0f, 1.0f, "%.2f",
            "Boundary line alpha.");
        sliderFloatWithHint("Thickness##bt", &visuals_.boundaryThickness, 0.5f, 6.0f, "%.1f",
            "Boundary line width in pixels.");
        ImGui::Unindent();
    }

    checkboxWithHint("Cell grid lines", &visuals_.showGrid,
        "Draw lines between cells. Visible only when cells are large enough.");
    if (visuals_.showGrid) {
        ImGui::Indent();
        sliderFloatWithHint("Grid opacity##go", &visuals_.gridOpacity, 0.0f, 1.0f, "%.2f",
            "Grid line transparency.");
        ImGui::Unindent();
        viz_.showCellGrid = true;
    } else {
        viz_.showCellGrid = false;
    }

    checkboxWithHint("Sparse overlay entries", &viz_.showSparseOverlay,
        "Include sparse overlay values (event patches, input patches) in the display.\n"
        "These appear as single-cell overrides on top of the base field.");
}

void drawOpticsSection() {
    ensureViewportStateConsistency();
    const int maxViewportIndex = std::max(0, static_cast<int>(viz_.viewports.size()) - 1);
    const std::size_t idx = static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, maxViewportIndex));
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
    sliderFloatWithHint("Brightness", &visuals_.brightness, 0.1f, 3.0f, "%.2f",
        "Post-colormap brightness multiplier.");
    sliderFloatWithHint("Contrast",   &visuals_.contrast,   0.1f, 3.0f, "%.2f",
        "Post-colormap contrast around mid-gray.");
    sliderFloatWithHint("Gamma",      &visuals_.gamma,       0.2f, 3.0f, "%.2f",
        "Display gamma correction applied after contrast/brightness.");
    checkboxWithHint("Invert colors", &visuals_.invertColors,
        "Invert the color mapping (useful for white-background screenshots).");
}

// Tab: Analysis
void drawAnalysisTab() {
    ImGui::BeginChild("AnalysisTabScroll", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);

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

void drawConservationSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Run the simulation to see conservation metrics.");
        return;
    }
    std::string msg;
    runtime_.status(msg);
    ImGui::TextWrapped("%s", msg.c_str());
}

void drawMetricsSection() {
    if (!runtime_.isRunning()) {
        ImGui::TextDisabled("Run the simulation to see metrics.");
        return;
    }
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
    sliderIntWithHint("Steps between display updates##metrics", &viz_.displayRefreshEveryNSteps, 1, 1000,
        "Refresh the display after every N simulation steps.\n"
        "A live latency cap keeps the display responsive even when N is large.");
    const float estStepsPerSec = estimatedSimulationStepsPerSecond();
    const float estRefreshesPerSec = estimatedDisplayRefreshesPerSecond();
    if (estStepsPerSec > 0.0f) {
        ImGui::TextDisabled("Estimated throughput: %.0f steps/sec, %.1f display updates/sec (<= %.0f ms latency)",
            estStepsPerSec, estRefreshesPerSec, displayRefreshLatencyCapMs());
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

    ImGui::EndChild();
}

void drawAdmissionSection() {
    std::string statusMsg;
    runtime_.status(statusMsg);
    ImGui::TextWrapped("%s", statusMsg.c_str());
}

void drawStepDiagSection() {
    std::string metricsMsg;
    runtime_.metrics(metricsMsg);
    ImGui::TextWrapped("%s", metricsMsg.c_str());
}

void drawOrderingLogSection() {
    ImGui::BeginChild("OrdLog", ImVec2(0, 160.0f), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    std::string statusMsg;
    runtime_.status(statusMsg);
    ImGui::TextWrapped("%s", statusMsg.c_str());
    ImGui::EndChild();
}

//
// Tab: System
//
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

void drawProfilesSection() {
    inputTextWithHint("Name##prof", panel_.profileName, sizeof(panel_.profileName),
        "Profile name for save / load. Letters, digits, '_', '-' only.");
    const float w3 = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (PrimaryButton("Save##prof", ImVec2(w3, 26.0f))) {
        std::string msg; runtime_.saveProfile(panel_.profileName, msg); appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Save the current launch configuration under this profile name.");
    ImGui::SameLine(0,4);
    if (PrimaryButton("Load##prof", ImVec2(w3, 26.0f))) {
        std::string msg;
        if (runtime_.loadProfile(panel_.profileName, msg)) {
            syncPanelFromConfig(); refreshFieldNames(); requestSnapshotRefresh();
        }
        appendLog(msg);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Load a previously saved profile. Use Restart to apply it.");
    ImGui::SameLine(0,4);
    if (SecondaryButton("List##prof", ImVec2(-1.0f, 26.0f))) {
        std::string msg; runtime_.listProfiles(msg); appendLog(msg);
    }
}

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

void drawShortcutsSection() {
    static constexpr struct { const char* key; const char* action; } kShortcuts[] = {
        {"Space",      "Toggle play / pause"},
        {"Ctrl+S",     "Save active world"},
        {"Right Arrow", "Step forward (when paused)"},
        {"R",          "Reset camera zoom and pan"},
        {"+  /  -",    "Zoom in / out"},
        {"F1 - F12",    "Select viewport for editing (first 12 views)"},
        {"Escape",     "Return keyboard focus to viewport"},
    };
    ImGui::Columns(2, "shortcols", false);
    ImGui::TextDisabled("Shortcut"); ImGui::NextColumn();
    ImGui::TextDisabled("Action"); ImGui::NextColumn();
    ImGui::Separator();
    for (const auto& s : kShortcuts) {
        ImGui::TextColored(ImVec4(0.8f,0.9f,0.5f,1.0f), "%s", s.key); ImGui::NextColumn();
        ImGui::TextUnformatted(s.action); ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

// Grid setup section (used by System tab)
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

    const float recommendationButtonW = (ImGui::GetContentRegionAvail().x - kS2) * 0.5f;
    if (SecondaryButton("Apply recommended defaults", ImVec2(recommendationButtonW, 24.0f))) {
        const InitialConditionType runtimeMode = fallbackRuntimeSupportedMode(refinedRecommendation);
        applyGenerationDefaultsForMode(panel_, sessionUi_.selectedModelCatalog, runtimeMode, true);
        applyAutoVariableBindingsForMode(panel_, modelCellVars, runtimeMode);
        viz_.generationPreviewDisplayType = recommendedPreviewDisplayTypeForMode(runtimeMode);
        sessionUi_.generationPreviewSourceIndex = recommendedPreviewSourceForMode(runtimeMode);
        sessionUi_.generationPreviewChannelIndex = findPreferredVariableIndex(
            modelCellVars,
            {"water", "state", "concentration", "temperature", "vegetation"},
            0);
        sessionUi_.generationModeIndex = static_cast<int>(runtimeMode);
        rebuildVariableInitializationSettings(sessionUi_, sessionUi_.selectedModelCatalog);
    }
    DelayedTooltip("Sets generation mode and parameters using model-aware defaults from metadata analysis.");
    ImGui::SameLine();
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

        const float actionW = (ImGui::GetContentRegionAvail().x - (3.0f * kS2)) * 0.25f;
        if (SecondaryButton("Enable all", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                setting.enabled = true;
            }
        }
        ImGui::SameLine();
        if (SecondaryButton("Disable all", ImVec2(actionW, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                setting.enabled = false;
            }
        }
        ImGui::SameLine();
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
        ImGui::SameLine();
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

        if (SecondaryButton("Enabled -> zero", ImVec2(120.0f, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                if (setting.enabled) {
                    setting.baseValue = 0.0f;
                }
            }
        }
        ImGui::SameLine();
        if (SecondaryButton("Enabled -> one", ImVec2(120.0f, 22.0f))) {
            for (auto& setting : sessionUi_.variableInitializationSettings) {
                if (setting.enabled) {
                    setting.baseValue = 1.0f;
                }
            }
        }

        ImGui::SetNextItemWidth(-1.0f);
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
}

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

void triggerOverlay(const OverlayIcon icon) {
    overlay_.icon  = icon;
    overlay_.alpha = 1.0f;
}

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

void appendLog(const std::string& line) {
    if (line.empty()) return;
    logs_.push_back(line);
    if (logs_.size() > 2000)
        logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 2000));
}

[[nodiscard]] std::filesystem::path displayPrefsPathForWorld(const std::string& w) const {
    if (w.empty()) return {};
    return std::filesystem::path("checkpoints") / "worlds" / (w + ".displayprefs");
}
[[nodiscard]] std::filesystem::path activeDisplayPrefsPath() const {
    return displayPrefsPathForWorld(runtime_.activeWorldName());
}

void openSelectedWorld() {
    appendLog("open_world_button_clicked");
    if (sessionUi_.selectedWorldIndex < 0 ||
        sessionUi_.selectedWorldIndex >= static_cast<int>(sessionUi_.worlds.size())) {
        appendLog("open_world_error selected_index_invalid");
        return;
    }
    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
    std::string msg;
    if (runtime_.openWorld(world.worldName, msg)) {
        appendLog(msg);
        refreshFieldNames();
        resetDisplayConfigToDefaults();
        loadDisplayPrefs();
        enterSimulationPaused();
    } else {
        appendLog(msg);
        std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", msg.c_str());
    }
}

void resetDisplayConfigToDefaults() {
    const VisualizationState defaults{};
    viz_.layout                      = defaults.layout;
    viz_.viewports                   = defaults.viewports;
    viz_.activeViewportEditor        = defaults.activeViewportEditor;
    viz_.displayRefreshEveryNSteps   = defaults.displayRefreshEveryNSteps;
    viz_.unlimitedSimSpeed           = defaults.unlimitedSimSpeed;
    viz_.displayManager              = defaults.displayManager;
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
    out << "displayAutoWaterLevel="     << static_cast<int>(viz_.displayManager.autoWaterLevel)  << "\n";
    out << "displayWaterThreshold="     << viz_.displayManager.waterLevel                        << "\n";
    out << "displayWaterQuantile="      << viz_.displayManager.autoWaterQuantile                 << "\n";
    out << "displayLowlandThreshold="   << viz_.displayManager.lowlandThreshold                  << "\n";
    out << "displayHighlandThreshold="  << viz_.displayManager.highlandThreshold                 << "\n";
    out << "displayWaterPresenceThreshold=" << viz_.displayManager.waterPresenceThreshold        << "\n";
    out << "displayShallowWaterDepth="  << viz_.displayManager.shallowWaterDepth                 << "\n";
    out << "displayHighMoistureThreshold=" << viz_.displayManager.highMoistureThreshold          << "\n";
    out << "displayRefreshEveryNSteps=" << viz_.displayRefreshEveryNSteps                       << "\n";
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
        out << "vp" << i << "_zoom="               << cam.zoom                         << "\n";
        out << "vp" << i << "_panX="               << cam.panX                         << "\n";
        out << "vp" << i << "_panY="               << cam.panY                         << "\n";
    }
}

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
            else if (key == "displayAutoWaterLevel")   viz_.displayManager.autoWaterLevel = (std::stoi(val) != 0);
            else if (key == "displayWaterThreshold")   viz_.displayManager.waterLevel = std::stof(val);
            else if (key == "displayWaterQuantile")    viz_.displayManager.autoWaterQuantile = std::stof(val);
            else if (key == "displayLowlandThreshold") viz_.displayManager.lowlandThreshold = std::stof(val);
            else if (key == "displayHighlandThreshold")viz_.displayManager.highlandThreshold = std::stof(val);
            else if (key == "displayWaterPresenceThreshold") viz_.displayManager.waterPresenceThreshold = std::stof(val);
            else if (key == "displayShallowWaterDepth") viz_.displayManager.shallowWaterDepth = std::stof(val);
            else if (key == "displayHighMoistureThreshold") viz_.displayManager.highMoistureThreshold = std::stof(val);
            else if (key == "displayRefreshEveryNSteps") viz_.displayRefreshEveryNSteps = std::stoi(val);
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
    viz_.displayManager.waterLevel = std::clamp(viz_.displayManager.waterLevel, 0.0f, 1.0f);
    viz_.displayManager.autoWaterQuantile = std::clamp(viz_.displayManager.autoWaterQuantile, 0.0f, 1.0f);
    viz_.displayManager.lowlandThreshold  = std::clamp(viz_.displayManager.lowlandThreshold,  0.0f, 1.0f);
    viz_.displayManager.highlandThreshold = std::clamp(
        viz_.displayManager.highlandThreshold,
        viz_.displayManager.lowlandThreshold + 0.01f, 1.0f);
    viz_.displayManager.waterPresenceThreshold = std::clamp(
        viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f);
    viz_.displayManager.shallowWaterDepth = std::clamp(
        viz_.displayManager.shallowWaterDepth, 0.0f, 0.5f);
    viz_.displayManager.highMoistureThreshold = std::clamp(
        viz_.displayManager.highMoistureThreshold, 0.0f, 1.0f);
    viz_.displayRefreshEveryNSteps = std::clamp(viz_.displayRefreshEveryNSteps, 1, 1000);
    ensureViewportStateConsistency();
    requestViewportEditorSelection(static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, static_cast<int>(viz_.viewports.size()) - 1)));
    clampVisualizationIndices();
}

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
