#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    enum class WorkflowRailAction { Models, Worlds, Create, Run, Analyze };

    [[nodiscard]] WorkflowRailAction workflowRailPrimaryAction(const bool hasModelContext, const bool hasRuntime) const {
        switch (appState_) {
            case AppState::ModelSelector:
            case AppState::ModelEditor:
                return WorkflowRailAction::Models;
            case AppState::SessionManager:
                return hasModelContext ? WorkflowRailAction::Worlds : WorkflowRailAction::Models;
            case AppState::NewWorldWizard:
                return hasModelContext ? WorkflowRailAction::Create : WorkflowRailAction::Models;
            case AppState::Simulation:
                if (!hasRuntime) {
                    return hasModelContext ? WorkflowRailAction::Worlds : WorkflowRailAction::Models;
                }
                return taskRailAnalyzeSelected_ ? WorkflowRailAction::Analyze : WorkflowRailAction::Run;
        }
        return WorkflowRailAction::Models;
    }

    [[nodiscard]] static const char* workflowRailActionLabel(const WorkflowRailAction action) {
        switch (action) {
            case WorkflowRailAction::Models: return "Browse models";
            case WorkflowRailAction::Worlds: return "Open worlds";
            case WorkflowRailAction::Create: return "Create world";
            case WorkflowRailAction::Run: return "Resume run";
            case WorkflowRailAction::Analyze: return "Open analysis";
        }
        return "Browse models";
    }

    [[nodiscard]] static const char* workflowRailActionTooltip(const WorkflowRailAction action) {
        switch (action) {
            case WorkflowRailAction::Models: return "Return to the model browser and choose a package.";
            case WorkflowRailAction::Worlds: return "Open the world workspace for the selected model.";
            case WorkflowRailAction::Create: return "Start the world creation wizard using the selected model.";
            case WorkflowRailAction::Run: return "Return to the runtime view and continue the current world.";
            case WorkflowRailAction::Analyze: return "Switch to the analysis-oriented simulation view.";
        }
        return "Return to the model browser and choose a package.";
    }

    [[nodiscard]] std::string workflowRailPendingContext() const {
        std::vector<std::string> pending;

        if (sessionUi_.operationActive) {
            pending.emplace_back("operation in progress");
        }
        if (sessionUi_.needsRefresh) {
            pending.emplace_back("world refresh queued");
        }
        if (sessionUi_.pendingDeleteWorldIndex >= 0 ||
            sessionUi_.pendingDuplicateWorldIndex >= 0 ||
            sessionUi_.pendingRenameWorldIndex >= 0 ||
            sessionUi_.pendingExportWorldIndex >= 0 ||
            sessionUi_.pendingImportPath[0] != '\0') {
            pending.emplace_back("world action pending");
        }
        if (deferredWizardInitialization_.active) {
            pending.emplace_back("wizard initialization pending");
        }
        if (showStopResetConfirm_ || showCheckpointDeleteConfirm_ || showWizardResetConfirm_) {
            pending.emplace_back("confirmation required");
        }
        if (asyncErrorPending_) {
            pending.emplace_back("runtime error review");
        } else if (asyncWarningPending_) {
            pending.emplace_back("runtime warning review");
        }

        if (pending.empty()) {
            return "none";
        }

        std::ostringstream out;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << pending[i];
        }
        return out.str();
    }

    [[nodiscard]] std::string workflowRailSummaryText(const bool hasModelContext, const bool hasRuntime) const {
        std::ostringstream out;
        out << "Stage: " << appStateLabel(static_cast<int>(appState_));
        out << " | Model: ";
        if (hasModelContext) {
            out << sessionUi_.selectedModelName;
        } else {
            out << "none selected";
        }

        if (hasRuntime) {
            const std::string worldName = runtime_.activeWorldName();
            out << " | World: ";
            out << (!worldName.empty() ? worldName : "active runtime");
        } else {
            out << " | World: none active";
        }

        const WorkflowRailAction primaryAction = workflowRailPrimaryAction(hasModelContext, hasRuntime);
        out << " | Primary: " << workflowRailActionLabel(primaryAction);
        out << " | Pending: " << workflowRailPendingContext();
        return out.str();
    }

    void activateWorkflowRailAction(const WorkflowRailAction action) {
        workflowRailAdvancedMode_ = false;
        switch (action) {
            case WorkflowRailAction::Models:
                appState_ = AppState::ModelSelector;
                modelEditor_.close();
                modelSelector_.open();
                taskRailAnalyzeSelected_ = false;
                break;
            case WorkflowRailAction::Worlds:
                appState_ = AppState::SessionManager;
                sessionUi_.needsRefresh = true;
                taskRailAnalyzeSelected_ = false;
                break;
            case WorkflowRailAction::Create: {
                const std::string baseHint = sessionUi_.selectedModelName[0] != '\0'
                    ? std::string(sessionUi_.selectedModelName)
                    : std::string("world");
                const std::string suggestedName = runtime_.suggestWorldNameFromHint(baseHint);
                std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", suggestedName.c_str());
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
                sessionUi_.wizardStepIndex = 0;
                deferredWizardInitialization_ = DeferredWizardInitialization{};
                appState_ = AppState::NewWorldWizard;
                taskRailAnalyzeSelected_ = false;
                break;
            }
            case WorkflowRailAction::Run:
                appState_ = AppState::Simulation;
                taskRailAnalyzeSelected_ = false;
                break;
            case WorkflowRailAction::Analyze:
                appState_ = AppState::Simulation;
                taskRailAnalyzeSelected_ = true;
                break;
        }
    }

    void drawTaskRailOverlay() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        if (appState_ != AppState::Simulation) {
            taskRailAnalyzeSelected_ = false;
        }

        const float topOffset = appState_ == AppState::Simulation ? 30.0f : 10.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + topOffset), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGuiWindowFlags railFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::Begin("Task Rail", nullptr, railFlags)) {
            ImGui::End();
            return;
        }

        const bool hasModelContext = sessionUi_.selectedModelName[0] != '\0';
        const bool hasRuntime = runtime_.isRunning();
        const WorkflowRailAction primaryAction = workflowRailPrimaryAction(hasModelContext, hasRuntime);
        const float btnW = 86.0f;
        const float btnH = 24.0f;

        ImGui::PushTextWrapPos(320.0f);
        ImGui::TextWrapped("%s", workflowRailSummaryText(hasModelContext, hasRuntime).c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("Primary path lock active.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!workflowRailAdvancedMode_) {
            if (PrimaryButton(workflowRailActionLabel(primaryAction), ImVec2(160.0f, 28.0f))) {
                activateWorkflowRailAction(primaryAction);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", workflowRailActionTooltip(primaryAction));
            }

            ImGui::SameLine();
            if (SecondaryButton("Advanced navigation", ImVec2(128.0f, 28.0f))) {
                workflowRailAdvancedMode_ = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Show the full navigation rail and alternate destinations.");
            }

            ImGui::End();
            return;
        }

        const auto drawTaskButton = [&](const char* label, const bool active, const bool enabled) {
            if (!enabled) {
                ImGui::BeginDisabled();
            }
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(46, 104, 172, 235));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 117, 189, 245));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(38, 89, 150, 255));
            }
            const bool pressed = ImGui::Button(label, ImVec2(btnW, btnH));
            if (active) {
                ImGui::PopStyleColor(3);
            }
            if (!enabled) {
                ImGui::EndDisabled();
            }
            return pressed;
        };

        if (SecondaryButton("Compact", ImVec2(88.0f, 24.0f))) {
            workflowRailAdvancedMode_ = false;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Return to the primary-path lock.");
        }

        ImGui::Spacing();

        const bool modelsActive = appState_ == AppState::ModelSelector || appState_ == AppState::ModelEditor;
        if (drawTaskButton("Models", modelsActive, true)) {
            activateWorkflowRailAction(WorkflowRailAction::Models);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Browse and choose model packages.");
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool worldsActive = appState_ == AppState::SessionManager;
        if (drawTaskButton("Worlds", worldsActive, hasModelContext)) {
            activateWorkflowRailAction(WorkflowRailAction::Worlds);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(hasModelContext
                ? "Open and manage saved worlds for the selected model."
                : "Select a model first to open world workspace.");
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool createActive = appState_ == AppState::NewWorldWizard;
        if (drawTaskButton("Create", createActive, hasModelContext)) {
            activateWorkflowRailAction(WorkflowRailAction::Create);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(hasModelContext
                ? "Create a new world using the selected model."
                : "Select a model first to create a world.");
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool runActive = appState_ == AppState::Simulation && !taskRailAnalyzeSelected_;
        if (drawTaskButton("Run", runActive, hasRuntime)) {
            activateWorkflowRailAction(WorkflowRailAction::Run);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(hasRuntime
                ? "Open runtime control and viewport panels."
                : "Start or open a world runtime to use Run.");
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool analyzeActive = appState_ == AppState::Simulation && taskRailAnalyzeSelected_;
        if (drawTaskButton("Analyze", analyzeActive, hasRuntime)) {
            activateWorkflowRailAction(WorkflowRailAction::Analyze);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(hasRuntime
                ? "Open simulation view and use the Analysis tab for diagnostics workflows."
                : "Start or open a world runtime to use Analyze.");
        }

        ImGui::End();
    }

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
