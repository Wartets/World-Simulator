#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    void drawSessionManager() {
        if (sessionUi_.needsRefresh) {
            std::string listMessage;
            sessionUi_.worlds = runtime_.listStoredWorlds(listMessage);
            if (sessionUi_.selectedWorldIndex >= static_cast<int>(sessionUi_.worlds.size())) {
                sessionUi_.selectedWorldIndex = static_cast<int>(sessionUi_.worlds.empty() ? -1 : 0);
            }
            if (sessionUi_.selectedWorldIndex < 0 && !sessionUi_.worlds.empty()) {
                sessionUi_.selectedWorldIndex = 0;
            }
            std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", listMessage.c_str());
            sessionUi_.needsRefresh = false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("Session Manager Space", nullptr, flags);

        const float rootW = std::max(640.0f, viewport->Size.x);
        const float rootH = std::max(420.0f, viewport->Size.y);
        const ImVec2 rootPos(0.0f, 0.0f);

        ImGui::SetCursorPos(rootPos);
        ImGui::BeginChild("SessionRoot", ImVec2(rootW, rootH), true);

        ImGui::BeginChild("SessionHeader", ImVec2(-1.0f, 88.0f), false);
        SectionHeader("World Workspace", "Open, inspect, and manage saved worlds.");
        const float headerBtnW = 170.0f;
        const float headerGap = 8.0f;
        ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionAvail().x - (headerBtnW * 2.0f + headerGap)));
        if (SecondaryButton("Back to models", ImVec2(headerBtnW, 32.0f))) {
            appState_ = AppState::ModelSelector;
            modelSelector_.open();
        }
        DelayedTooltip("Return to the model selection screen.");
        ImGui::SameLine(0.0f, headerGap);
        if (SecondaryButton("Refresh list", ImVec2(headerBtnW, 32.0f))) {
            sessionUi_.needsRefresh = true;
        }
        DelayedTooltip("Refreshes the world list from stored profiles and checkpoints.");
        ImGui::EndChild();

        if (sessionUi_.selectedModelName[0] != '\0') {
            ImGui::BeginChild("SessionModelContext", ImVec2(-1.0f, 84.0f), true);
            ImGui::Text("Selected model: %s", sessionUi_.selectedModelName);
            ImGui::TextDisabled("Author: %s    Version: %s", sessionUi_.selectedModelAuthor, sessionUi_.selectedModelVersion);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextDisabled("%s", sessionUi_.selectedModelDescription);
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float actionBarH = 138.0f;
        const float contentH = std::max(220.0f, ImGui::GetContentRegionAvail().y - actionBarH - kS3);
        const bool narrowLayout = rootW < 1100.0f;

        std::vector<int> filteredWorldIndices;
        filteredWorldIndices.reserve(sessionUi_.worlds.size());
        const std::string query = app::toLower(std::string(sessionUi_.worldSearch));
        for (int i = 0; i < static_cast<int>(sessionUi_.worlds.size()); ++i) {
            const auto& world = sessionUi_.worlds[static_cast<std::size_t>(i)];
            if (sessionUi_.filterCheckpointOnly && !world.hasCheckpoint) {
                continue;
            }
            if (sessionUi_.filterProfileOnly && !world.hasProfile) {
                continue;
            }
            if (!query.empty()) {
                const std::string worldName = app::toLower(world.worldName);
                const std::string mode = app::toLower(world.initialConditionMode);
                if (worldName.find(query) == std::string::npos && mode.find(query) == std::string::npos) {
                    continue;
                }
            }
            filteredWorldIndices.push_back(i);
        }

        auto sortByMode = [&](int lhs, int rhs) {
            const auto& a = sessionUi_.worlds[static_cast<std::size_t>(lhs)];
            const auto& b = sessionUi_.worlds[static_cast<std::size_t>(rhs)];
            if (sessionUi_.sortMode == 0) {
                return app::toLower(a.worldName) < app::toLower(b.worldName);
            }
            if (sessionUi_.sortMode == 2) {
                const std::uint64_t areaA = static_cast<std::uint64_t>(a.gridWidth) * static_cast<std::uint64_t>(a.gridHeight);
                const std::uint64_t areaB = static_cast<std::uint64_t>(b.gridWidth) * static_cast<std::uint64_t>(b.gridHeight);
                if (areaA != areaB) {
                    return areaA > areaB;
                }
                return app::toLower(a.worldName) < app::toLower(b.worldName);
            }
            if (sessionUi_.sortMode == 3) {
                const std::uintmax_t bytesA = a.profileBytes + a.checkpointBytes;
                const std::uintmax_t bytesB = b.profileBytes + b.checkpointBytes;
                if (bytesA != bytesB) {
                    return bytesA > bytesB;
                }
                return app::toLower(a.worldName) < app::toLower(b.worldName);
            }

            const auto timeA = a.hasCheckpointTimestamp ? a.checkpointLastWrite : a.profileLastWrite;
            const auto timeB = b.hasCheckpointTimestamp ? b.checkpointLastWrite : b.profileLastWrite;
            if (timeA != timeB) {
                return timeA > timeB;
            }
            return app::toLower(a.worldName) < app::toLower(b.worldName);
        };
        std::sort(filteredWorldIndices.begin(), filteredWorldIndices.end(), sortByMode);

        ImGui::BeginChild("SessionContent", ImVec2(-1.0f, contentH), false);
        if (!narrowLayout) {
            ImGui::Columns(2, "session_cols", false);
            ImGui::SetColumnWidth(0, rootW * 0.38f);
        }

        SectionHeader("Saved worlds", "Search, filter, and select a world.");
        ImGui::InputTextWithHint("##world_search", "Search by name or mode", sessionUi_.worldSearch, IM_ARRAYSIZE(sessionUi_.worldSearch));
        ImGui::SameLine();
        static constexpr const char* kSortModes[] = {"Name", "Recent", "Grid size", "Storage size"};
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("##world_sort", &sessionUi_.sortMode, kSortModes, static_cast<int>(std::size(kSortModes)));
        ImGui::SameLine();
        ImGui::Checkbox("Checkpoint", &sessionUi_.filterCheckpointOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Profile", &sessionUi_.filterProfileOnly);
        DelayedTooltip("Filter worlds requiring checkpoint/profile presence.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldList", ImVec2(-1.0f, narrowLayout ? 210.0f : -1.0f), true);
        if (filteredWorldIndices.empty()) {
            EmptyStateCard("No saved worlds found.", "Create a new world to generate your first simulation.");
        } else {
            for (int i = 0; i < static_cast<int>(filteredWorldIndices.size()); ++i) {
                const int worldIndex = filteredWorldIndices[static_cast<std::size_t>(i)];
                const auto& world = sessionUi_.worlds[static_cast<std::size_t>(worldIndex)];
                const bool selected = (sessionUi_.selectedWorldIndex == worldIndex);
                if (ImGui::Selectable(world.worldName.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 30.0f))) {
                    sessionUi_.selectedWorldIndex = worldIndex;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openSelectedWorld();
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("  %s%s", world.initialConditionMode.empty() ? "n/a" : world.initialConditionMode.c_str(), world.hasCheckpoint ? "  [cp]" : "");
            }
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("World details", "Review metadata, storage, and checkpoint status.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldDetails", ImVec2(-1.0f, -1.0f), true);
        if (sessionUi_.selectedWorldIndex >= 0 && sessionUi_.selectedWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
            const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            const std::uint64_t gridCells = static_cast<std::uint64_t>(world.gridWidth) * static_cast<std::uint64_t>(world.gridHeight);
            const std::uintmax_t totalBytes = world.profileBytes + world.checkpointBytes;
            ImGui::Text("Name: %s", world.worldName.c_str());
            ImGui::Text("Grid: %ux%u", world.gridWidth, world.gridHeight);
            ImGui::Text("Cells: %llu", static_cast<unsigned long long>(gridCells));
            ImGui::Text("Seed: %llu", static_cast<unsigned long long>(world.seed));
            ImGui::Text("Temporal: %s", world.temporalPolicy.empty() ? "n/a" : world.temporalPolicy.c_str());
            ImGui::Text("Initialization: %s", world.initialConditionMode.empty() ? "n/a" : world.initialConditionMode.c_str());
            ImGui::Text("Data footprint: %s", session_manager::formatBytes(totalBytes).c_str());
            ImGui::Text("Profile: %s", world.profilePath.string().c_str());
            ImGui::Text("Profile size: %s", session_manager::formatBytes(world.profileBytes).c_str());
            ImGui::Text("Profile updated: %s", session_manager::formatFileTime(world.profileLastWrite, world.hasProfileTimestamp).c_str());
            ImGui::Text("Checkpoint available: %s", world.hasCheckpoint ? "Yes" : "No");
            if (world.hasCheckpoint) {
                ImGui::Text("Last saved step: %llu", static_cast<unsigned long long>(world.stepIndex));
                ImGui::Text("Run identity: %016llx", static_cast<unsigned long long>(world.runIdentityHash));
                ImGui::Text("Checkpoint size: %s", session_manager::formatBytes(world.checkpointBytes).c_str());
                ImGui::Text("Checkpoint updated: %s", session_manager::formatFileTime(world.checkpointLastWrite, world.hasCheckpointTimestamp).c_str());
            } else {
                LabeledHint("This world will open from profile defaults because no checkpoint is available.");
            }

            const int qualityScore =
                (world.hasProfile ? 40 : 0) +
                (world.hasCheckpoint ? 40 : 0) +
                (world.gridWidth > 0 && world.gridHeight > 0 ? 20 : 0);
            ImGui::Spacing();
            ImGui::ProgressBar(static_cast<float>(qualityScore) / 100.0f, ImVec2(-1.0f, 0.0f));
            ImGui::TextDisabled("Integrity score: %d/100", qualityScore);
        } else {
            EmptyStateCard("No world selected.", "Select a world from the list to enable the Open world action.");
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::Columns(1);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const bool canOpen = sessionUi_.selectedWorldIndex >= 0 && sessionUi_.selectedWorldIndex < static_cast<int>(sessionUi_.worlds.size());
        ImGui::BeginChild("SessionActions", ImVec2(-1.0f, actionBarH), false);
        const float w = ImGui::GetContentRegionAvail().x;
        const float btnW = std::max(132.0f, (w - (5.0f * kS2)) / 6.0f);

        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (PrimaryButton("Open world", ImVec2(btnW, 42.0f))) {
            openSelectedWorld();
        }
        DelayedTooltip("Loads the selected world without restarting the application.");
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (SecondaryButton("Create world", ImVec2(btnW, 42.0f))) {
            const std::string baseHint = sessionUi_.selectedModelName[0] != '\0'
                ? std::string(sessionUi_.selectedModelName)
                : std::string("world");
            const std::string suggestedName = runtime_.suggestWorldNameFromHint(baseHint);
            std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", suggestedName.c_str());
            syncPanelFromConfig();
            panel_.useManualSeed = false;
            panel_.seed = generateRandomSeed();
            appState_ = AppState::NewWorldWizard;
        }
        DelayedTooltip("Open world creation with smart default naming based on the selected model.");

        ImGui::SameLine();
        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (SecondaryButton("Duplicate", ImVec2(btnW, 42.0f))) {
            sessionUi_.pendingDuplicateWorldIndex = sessionUi_.selectedWorldIndex;
            if (canOpen) {
                const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                const std::string suggested = runtime_.suggestWorldNameFromHint(selected.worldName + "_copy");
                std::snprintf(sessionUi_.pendingDuplicateName, sizeof(sessionUi_.pendingDuplicateName), "%s", suggested.c_str());
            }
            ImGui::OpenPopup("Duplicate World");
        }
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (SecondaryButton("Rename", ImVec2(btnW, 42.0f))) {
            sessionUi_.pendingRenameWorldIndex = sessionUi_.selectedWorldIndex;
            if (canOpen) {
                const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                std::snprintf(sessionUi_.pendingRenameName, sizeof(sessionUi_.pendingRenameName), "%s", selected.worldName.c_str());
            }
            ImGui::OpenPopup("Rename World");
        }
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (SecondaryButton("Export", ImVec2(btnW, 42.0f))) {
            sessionUi_.pendingExportWorldIndex = sessionUi_.selectedWorldIndex;
            if (canOpen) {
                const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                std::snprintf(sessionUi_.pendingExportPath, sizeof(sessionUi_.pendingExportPath), "exports/%s.wsexp", selected.worldName.c_str());
            }
            ImGui::OpenPopup("Export World");
        }
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (SecondaryButton("Delete world", ImVec2(btnW, 42.0f))) {
            sessionUi_.pendingDeleteWorldIndex = sessionUi_.selectedWorldIndex;
            ImGui::OpenPopup("Delete World Confirm");
        }
        DelayedTooltip("Deletes profile/checkpoint/display settings for the selected world.");
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        if (ImGui::BeginPopupModal("Duplicate World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("New world name", sessionUi_.pendingDuplicateName, IM_ARRAYSIZE(sessionUi_.pendingDuplicateName));
            if (PrimaryButton("Duplicate", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingDuplicateWorldIndex >= 0 && sessionUi_.pendingDuplicateWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingDuplicateWorldIndex)];
                    std::string message;
                    const std::string requestedName = sessionUi_.pendingDuplicateName;
                    const std::string normalizedName = runtime_.normalizeWorldNameForUi(requestedName);
                    if (normalizedName.empty()) {
                        message = "world_duplicate_failed error=invalid_name";
                    } else {
                        if (requestedName != normalizedName) {
                            std::snprintf(sessionUi_.pendingDuplicateName, sizeof(sessionUi_.pendingDuplicateName), "%s", normalizedName.c_str());
                        }
                        runtime_.duplicateWorld(world.worldName, normalizedName, message);
                    }
                    std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
                    appendLog(message);
                    sessionUi_.needsRefresh = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Rename World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("World name", sessionUi_.pendingRenameName, IM_ARRAYSIZE(sessionUi_.pendingRenameName));
            if (PrimaryButton("Rename", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingRenameWorldIndex >= 0 && sessionUi_.pendingRenameWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingRenameWorldIndex)];
                    std::string message;
                    const std::string requestedName = sessionUi_.pendingRenameName;
                    const std::string normalizedName = runtime_.normalizeWorldNameForUi(requestedName);
                    if (normalizedName.empty()) {
                        message = "world_rename_failed error=invalid_name";
                    } else {
                        if (requestedName != normalizedName) {
                            std::snprintf(sessionUi_.pendingRenameName, sizeof(sessionUi_.pendingRenameName), "%s", normalizedName.c_str());
                        }
                        runtime_.renameWorld(world.worldName, normalizedName, message);
                    }
                    std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
                    appendLog(message);
                    sessionUi_.needsRefresh = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Export World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Export path", sessionUi_.pendingExportPath, IM_ARRAYSIZE(sessionUi_.pendingExportPath));
            if (PrimaryButton("Export", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingExportWorldIndex >= 0 && sessionUi_.pendingExportWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingExportWorldIndex)];
                    std::string message;
                    runtime_.exportWorld(world.worldName, std::filesystem::path(sessionUi_.pendingExportPath), message);
                    std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
                    appendLog(message);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(0.0f, 0.0f);
        if (SecondaryButton("Import", ImVec2(btnW, 42.0f))) {
            ImGui::OpenPopup("Import World");
        }
        if (ImGui::BeginPopupModal("Import World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Import file", sessionUi_.pendingImportPath, IM_ARRAYSIZE(sessionUi_.pendingImportPath));
            if (PrimaryButton("Import", ImVec2(140.0f, 28.0f))) {
                std::string importedName;
                std::string message;
                runtime_.importWorld(std::filesystem::path(sessionUi_.pendingImportPath), importedName, message);
                std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
                appendLog(message);
                sessionUi_.needsRefresh = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Delete World Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete selected world and all associated data?");
            if (sessionUi_.pendingDeleteWorldIndex >= 0 && sessionUi_.pendingDeleteWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingDeleteWorldIndex)];
                ImGui::Text("World: %s", world.worldName.c_str());
            }
            ImGui::Separator();
            if (PrimaryButton("Delete permanently", ImVec2(170.0f, 28.0f))) {
                if (sessionUi_.pendingDeleteWorldIndex >= 0 && sessionUi_.pendingDeleteWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingDeleteWorldIndex)];
                    std::string message;
                    runtime_.deleteWorld(world.worldName, message);
                    appendLog(message);
                    std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
                    sessionUi_.needsRefresh = true;
                }
                sessionUi_.pendingDeleteWorldIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(110.0f, 28.0f))) {
                sessionUi_.pendingDeleteWorldIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!canOpen) {
            LabeledHint("Open world is disabled until a world is selected.");
        }
        if (sessionUi_.statusMessage[0] != '\0') {
            LabeledHint(sessionUi_.statusMessage);
        }

        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();
    }

    void drawNewWorldWizard() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("New World Wizard", nullptr, flags);

        const float rootW = std::min(kPageMaxWidth, viewport->Size.x - (2.0f * kS5));
        const float rootH = std::max(420.0f, viewport->Size.y - (2.0f * kS5));
        const ImVec2 rootPos(viewport->Size.x * 0.5f - rootW * 0.5f, kS5);

        ImGui::SetCursorPos(rootPos);
        ImGui::BeginChild("WizardRoot", ImVec2(rootW, rootH), true);

        ImGui::BeginChild("WizardHeader", ImVec2(-1.0f, 88.0f), false);
        SectionHeader("Create New World", "Configure generation parameters before simulation start.");
        ImGui::EndChild();

        if (sessionUi_.selectedModelName[0] != '\0') {
            ImGui::BeginChild("WizardModelContext", ImVec2(-1.0f, 84.0f), true);
            ImGui::Text("Model context: %s", sessionUi_.selectedModelName);
            ImGui::TextDisabled("Author: %s    Version: %s", sessionUi_.selectedModelAuthor, sessionUi_.selectedModelVersion);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextDisabled("%s", sessionUi_.selectedModelDescription);
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float actionBarH = 98.0f;
        const float contentH = std::max(240.0f, ImGui::GetContentRegionAvail().y - actionBarH - kS3);
        const bool narrowLayout = rootW < 1150.0f;

        ImGui::BeginChild("WizardContent", ImVec2(-1.0f, contentH), false);
        if (!narrowLayout) {
            ImGui::Columns(2, "wizard_cols", false);
            ImGui::SetColumnWidth(0, rootW * 0.42f);
        }

        SectionHeader("Generation parameters", "Define initial conditions and model setup.");
        ImGui::Spacing();
        ImGui::BeginChild("WizardForm", ImVec2(-1.0f, narrowLayout ? 320.0f : -1.0f), true);
        inputTextWithHint("World Name", sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "Name for this world. Letters, numbers, '_' and '-' are recommended.");
        LabeledHint("Smart naming uses model context and avoids collisions automatically.");
        ImGui::Spacing();
        drawGridSetupSection();
        ImGui::Spacing();
        drawWorldGenerationSection();

        initialization::InitializationRequest bindingRequest;
        bindingRequest.type = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
        bindingRequest.conwayTargetOverride = std::string(panel_.conwayTargetVariable);
        bindingRequest.grayTargetAOverride = std::string(panel_.grayScottTargetVariableA);
        bindingRequest.grayTargetBOverride = std::string(panel_.grayScottTargetVariableB);
        bindingRequest.wavesTargetOverride = std::string(panel_.wavesTargetVariable);
        sessionUi_.generationBindingPlan = initialization::buildBindingPlan(sessionUi_.selectedModelCatalog, bindingRequest);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        SectionHeader("Initialization binding preview", "Validate model-variable bindings before world creation.");
        if (sessionUi_.selectedModelCellStateVariables.empty()) {
            LabeledHint("Model variable scan unavailable. Manual variable IDs are required.");
        } else {
            ImGui::TextDisabled(
                "Detected model cell-state variables: %d",
                static_cast<int>(sessionUi_.selectedModelCellStateVariables.size()));
        }
        if (!sessionUi_.generationBindingPlan.decisions.empty()) {
            for (const auto& decision : sessionUi_.generationBindingPlan.decisions) {
                ImGui::BulletText(
                    "%s -> %s  (confidence %.2f)",
                    decision.bindingKey.c_str(),
                    decision.variableId.empty() ? "<unresolved>" : decision.variableId.c_str(),
                    decision.confidence);
                ImGui::TextDisabled("    %s", decision.rationale.c_str());
            }
        }

        if (!sessionUi_.generationBindingPlan.hasBlockingIssues()) {
            ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Binding status: ready");
        } else {
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                "Binding status: unresolved (%d)",
                static_cast<int>(sessionUi_.generationBindingPlan.issues.size()));
            for (const auto& issue : sessionUi_.generationBindingPlan.issues) {
                ImGui::BulletText("%s: %s", issue.code.c_str(), issue.message.c_str());
            }
        }

        checkboxWithHint(
            "Expert override unresolved bindings",
            &sessionUi_.allowUnresolvedGenerationBindings,
            "Allows world creation even when bindings are unresolved. Use only for custom advanced workflows.");
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("Generation preview", "Preview only - simulation is not running.");
        ImGui::Spacing();
        ImGui::BeginChild("WizardPreview", ImVec2(-1.0f, -1.0f), true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 minPos = ImGui::GetCursorScreenPos();
        const ImVec2 maxPos(minPos.x + avail.x, minPos.y + avail.y);
        dl->AddRectFilled(minPos, maxPos, IM_COL32(12, 14, 24, 255), 4.0f);

        const int previewBaseW = std::max(48, panel_.gridWidth);
        const int previewBaseH = std::max(48, panel_.gridHeight);
        const bool interactivePreview = uiParameterInteractingThisFrame_ || ImGui::IsAnyItemActive();
        const std::uint64_t previewBudget = interactivePreview ? 70000ull : 130000ull;
        int previewStride = 1;
        while ((static_cast<std::uint64_t>(previewBaseW) / static_cast<std::uint64_t>(previewStride)) *
               (static_cast<std::uint64_t>(previewBaseH) / static_cast<std::uint64_t>(previewStride)) > previewBudget) {
            ++previewStride;
        }
        const int previewW = std::max(48, previewBaseW / previewStride);
        const int previewH = std::max(48, previewBaseH / previewStride);
        const float targetAspect = static_cast<float>(previewW) / static_cast<float>(previewH);
        float drawW = avail.x;
        float drawH = drawW / std::max(0.001f, targetAspect);
        if (drawH > avail.y) {
            drawH = avail.y;
            drawW = drawH * targetAspect;
        }
        const float drawX = minPos.x + (avail.x - drawW) * 0.5f;
        const float drawY = minPos.y + (avail.y - drawH) * 0.5f;
        std::uint64_t previewHash = 1469598103934665603ull;
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.seed));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewBaseW));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewBaseH));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewStride));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(viz_.generationPreviewDisplayType));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(viz_.displayManager.autoWaterLevel));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.waterLevel));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.autoWaterQuantile));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.lowlandThreshold));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.highlandThreshold));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.waterPresenceThreshold));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.shallowWaterDepth));
        previewHash = hashCombine(previewHash, hashFloat(viz_.displayManager.highMoistureThreshold));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainBaseFrequency));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainDetailFrequency));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainWarpStrength));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainAmplitude));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainRidgeMix));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.terrainOctaves));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainLacunarity));
        previewHash = hashCombine(previewHash, hashFloat(panel_.terrainGain));
        previewHash = hashCombine(previewHash, hashFloat(panel_.seaLevel));
        previewHash = hashCombine(previewHash, hashFloat(panel_.polarCooling));
        previewHash = hashCombine(previewHash, hashFloat(panel_.latitudeBanding));
        previewHash = hashCombine(previewHash, hashFloat(panel_.humidityFromWater));
        previewHash = hashCombine(previewHash, hashFloat(panel_.biomeNoiseStrength));
        previewHash = hashCombine(previewHash, hashFloat(panel_.islandDensity));
        previewHash = hashCombine(previewHash, hashFloat(panel_.islandFalloff));
        previewHash = hashCombine(previewHash, hashFloat(panel_.coastlineSharpness));
        previewHash = hashCombine(previewHash, hashFloat(panel_.archipelagoJitter));
        previewHash = hashCombine(previewHash, hashFloat(panel_.erosionStrength));
        previewHash = hashCombine(previewHash, hashFloat(panel_.shelfDepth));

        if (previewHash != wizardPreviewHash_ || wizardPreviewW_ != previewW || wizardPreviewH_ != previewH) {
            std::vector<float> previewTerrain;
            std::vector<float> previewWater;
            previewTerrain.reserve(static_cast<std::size_t>(previewW) * static_cast<std::size_t>(previewH));
            previewWater.reserve(static_cast<std::size_t>(previewW) * static_cast<std::size_t>(previewH));

            for (int y = 0; y < previewH; ++y) {
                for (int x = 0; x < previewW; ++x) {
                    const float terrain = previewTerrainValue(panel_, x * previewStride, y * previewStride, previewBaseW, previewBaseH);
                    const float water = std::clamp((panel_.seaLevel - terrain) * 1.9f + 0.10f, 0.0f, 1.0f);
                    previewTerrain.push_back(terrain);
                    previewWater.push_back(water);
                }
            }

            DisplayBuffer previewDisplay = buildDisplayBufferFromTerrain(
                previewTerrain,
                previewWater,
                viz_.generationPreviewDisplayType,
                viz_.displayManager,
                "preview");

            const std::size_t pixelCount = static_cast<std::size_t>(previewW) * static_cast<std::size_t>(previewH);
            wizardPreviewPixels_.assign(pixelCount * 4u, 0u);
            for (std::size_t idx = 0; idx < pixelCount; ++idx) {
                const float value = idx < previewDisplay.values.size() ? previewDisplay.values[idx] : 0.0f;
                const float normalized = std::clamp((value - previewDisplay.minValue) / std::max(0.0001f, previewDisplay.maxValue - previewDisplay.minValue), 0.0f, 1.0f);
                const ImU32 color = mapDisplayTypeColor(
                    (viz_.generationPreviewDisplayType == DisplayType::ScalarField || viz_.generationPreviewDisplayType == DisplayType::WaterDepth) ? normalized : value,
                    viz_.generationPreviewDisplayType,
                    ColorMapMode::Turbo);
                std::uint8_t r = 0, g = 0, b = 0, a = 0;
                unpackColor(color, r, g, b, a);
                const std::size_t o = idx * 4u;
                wizardPreviewPixels_[o + 0] = r;
                wizardPreviewPixels_[o + 1] = g;
                wizardPreviewPixels_[o + 2] = b;
                wizardPreviewPixels_[o + 3] = a;
            }

            uploadRasterTexture(wizardPreviewTexture_, previewW, previewH, wizardPreviewPixels_);
            wizardPreviewWaterLevel_ = previewDisplay.effectiveWaterLevel;
            wizardPreviewStride_ = previewStride;
            wizardPreviewW_ = previewW;
            wizardPreviewH_ = previewH;
            wizardPreviewHash_ = previewHash;
        }

        if (wizardPreviewTexture_.id != 0) {
            dl->AddImage(
                static_cast<ImTextureID>(wizardPreviewTexture_.id),
                ImVec2(drawX, drawY),
                ImVec2(drawX + drawW, drawY + drawH),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }

        const std::string previewLabel = std::string("Preview mode: ") + displayTypeLabel(viz_.generationPreviewDisplayType);
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2), IM_COL32(235, 240, 255, 255), previewLabel.c_str());
        const std::string autoLevel = "Water level: " + std::to_string(wizardPreviewWaterLevel_).substr(0, 5) + (viz_.displayManager.autoWaterLevel ? " (auto)" : " (manual)");
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 18.0f), IM_COL32(188, 200, 226, 255), autoLevel.c_str());
        if (wizardPreviewStride_ > 1) {
            const std::string quality = "Preview stride: 1/" + std::to_string(wizardPreviewStride_) + " for performance";
            dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 36.0f), IM_COL32(188, 200, 226, 255), quality.c_str());
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::Columns(1);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("WizardActions", ImVec2(-1.0f, actionBarH), false);
        const float w = ImGui::GetContentRegionAvail().x;
        const float btnW = std::max(170.0f, (w - (2.0f * kS2)) / 3.0f);

        if (PrimaryButton("Create world", ImVec2(btnW, 44.0f))) {
            if (!panel_.useManualSeed) {
                panel_.seed = generateRandomSeed();
            }

            initialization::InitializationRequest verifyRequest;
            verifyRequest.type = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
            verifyRequest.conwayTargetOverride = std::string(panel_.conwayTargetVariable);
            verifyRequest.grayTargetAOverride = std::string(panel_.grayScottTargetVariableA);
            verifyRequest.grayTargetBOverride = std::string(panel_.grayScottTargetVariableB);
            verifyRequest.wavesTargetOverride = std::string(panel_.wavesTargetVariable);
            sessionUi_.generationBindingPlan = initialization::buildBindingPlan(sessionUi_.selectedModelCatalog, verifyRequest);

            if (sessionUi_.generationBindingPlan.hasBlockingIssues() && !sessionUi_.allowUnresolvedGenerationBindings) {
                std::ostringstream status;
                status << "world_create_blocked unresolved_bindings=";
                for (std::size_t i = 0; i < sessionUi_.generationBindingPlan.issues.size(); ++i) {
                    if (i > 0) {
                        status << ',';
                    }
                    status << sessionUi_.generationBindingPlan.issues[i].code;
                }
                std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", status.str().c_str());
                appendLog(status.str());
                return;
            }

            applyConfigFromPanel();
            std::string worldName = runtime_.normalizeWorldNameForUi(sessionUi_.pendingWorldName);
            if (worldName.empty()) {
                const std::string baseHint = sessionUi_.selectedModelName[0] != '\0'
                    ? std::string(sessionUi_.selectedModelName)
                    : std::string("world");
                worldName = runtime_.suggestWorldNameFromHint(baseHint);
            }
            if (worldName != sessionUi_.pendingWorldName) {
                std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", worldName.c_str());
            }

            std::string message;
            if (runtime_.createWorld(worldName, runtime_.config(), message)) {
                appendLog(message);
                refreshFieldNames();
                resetDisplayConfigToDefaults();
                loadDisplayPrefs();
                enterSimulationPaused();
                sessionUi_.needsRefresh = true;
            } else {
                appendLog(message);
                std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
            }
        }
        DelayedTooltip("Applies generation settings, creates the world, and enters simulation view.");

        ImGui::SameLine();
        if (SecondaryButton("Back to world selection", ImVec2(btnW, 44.0f))) {
            appState_ = AppState::SessionManager;
        }
        DelayedTooltip("Return to world selection without creating a world.");

        ImGui::SameLine();
        if (SecondaryButton("Reset parameters", ImVec2(btnW, 44.0f))) {
            syncPanelFromConfig();
            panel_.useManualSeed = false;
            panel_.seed = generateRandomSeed();
            const std::string baseHint = sessionUi_.selectedModelName[0] != '\0'
                ? std::string(sessionUi_.selectedModelName)
                : std::string("world");
            std::string suggestedName = runtime_.suggestWorldNameFromHint(baseHint);
            std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", suggestedName.c_str());
        }
        DelayedTooltip("Restores parameters from the current runtime configuration.");

        if (sessionUi_.statusMessage[0] != '\0') {
            LabeledHint(sessionUi_.statusMessage);
        } else {
            LabeledHint("Create world starts simulation only after all settings are finalized.");
        }

        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();
    }

    void drawDockSpace() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("DockSpaceHost", nullptr, flags);
    #ifdef IMGUI_HAS_DOCK
        const ImGuiID dockspaceId = ImGui::GetID("RuntimeDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        if (!dockLayoutInitialized_) {
            dockLayoutInitialized_ = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.65f, nullptr, &dockMain);
            ImGuiID dockBottomLeft = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.5f, nullptr, &dockLeft);
            ImGuiID dockTopRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.55f, nullptr, &dockMain);

            ImGui::DockBuilderDockWindow("Runtime View 1", dockLeft);
            ImGui::DockBuilderDockWindow("Runtime View 2", dockBottomLeft);
            ImGui::DockBuilderDockWindow("Runtime View 3", dockTopRight);
            ImGui::DockBuilderDockWindow("Runtime View 4", dockMain);
            ImGui::DockBuilderDockWindow("Control Panel##main", dockMain);
            ImGui::DockBuilderFinish(dockspaceId);
        }
#endif
        ImGui::PopStyleVar(3);
        ImGui::End();
    }

    void settingHint(const char* text) const {
        if (text != nullptr && text[0] != '\0') {
            DelayedTooltip(text);
        }
    }

    void markParameterInteraction(const bool changed) {
        if (changed) {
            uiParameterChangedThisFrame_ = true;
        }
        if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
            uiParameterInteractingThisFrame_ = true;
            uiInteractionHotUntilSec_ = std::max(uiInteractionHotUntilSec_, glfwGetTime() + 0.18);
        }
    }

    bool checkboxWithHint(const char* label, bool* value, const char* hint) {
        const bool changed = ImGui::Checkbox(label, value);
        markParameterInteraction(changed);
        settingHint(hint);
        return changed;
    }

    bool sliderFloatWithHint(const char* label, float* value, const float minV, const float maxV, const char* format, const char* hint) {
        const bool changed = NumericSliderPair(label, value, minV, maxV, format);
        markParameterInteraction(changed);
        settingHint(hint);
        return changed;
    }

    bool sliderIntWithHint(const char* label, int* value, const int minV, const int maxV, const char* hint) {
        const bool changed = NumericSliderPairInt(label, value, minV, maxV);
        markParameterInteraction(changed);
        settingHint(hint);
        return changed;
    }

    bool inputTextWithHint(const char* label, char* buffer, const std::size_t size, const char* hint) {
        const bool changed = ImGui::InputText(label, buffer, size);
        markParameterInteraction(changed);
        settingHint(hint);
        return changed;
    }

    [[nodiscard]] static std::uint64_t generateRandomSeed() {
        std::random_device rd;
        const std::uint64_t hi = static_cast<std::uint64_t>(rd());
        const std::uint64_t lo = static_cast<std::uint64_t>(rd());
        const std::uint64_t mix = (hi << 32u) ^ lo ^ static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return mix == 0 ? 1ull : mix;
    }

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
