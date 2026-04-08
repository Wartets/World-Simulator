#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    void beginOperationStatus(const char* label, const float progress = -1.0f, const char* detail = "") {
        std::snprintf(sessionUi_.operationLabel, sizeof(sessionUi_.operationLabel), "%s", label != nullptr ? label : "operation");
        std::snprintf(sessionUi_.operationDetail, sizeof(sessionUi_.operationDetail), "%s", detail != nullptr ? detail : "");
        sessionUi_.operationProgress = progress;
        sessionUi_.operationActive = true;
    }

    void completeOperationStatus(
        const std::chrono::steady_clock::time_point startedAt,
        const char* completionDetail = "") {
        const float elapsedMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count());
        sessionUi_.lastOperationDurationMs = elapsedMs;
        sessionUi_.operationProgress = 1.0f;
        sessionUi_.operationActive = false;

        std::ostringstream detail;
        if (completionDetail != nullptr && completionDetail[0] != '\0') {
            detail << completionDetail << " | ";
        }
        detail << "duration_ms=" << static_cast<int>(elapsedMs);
        std::snprintf(sessionUi_.operationDetail, sizeof(sessionUi_.operationDetail), "%s", detail.str().c_str());
    }

    [[nodiscard]] static const char* worldStorageStatusLabel(const StoredWorldInfo& world) {
        if (!world.hasProfile && !world.hasCheckpoint) {
            return "Storage incomplete";
        }
        if (world.hasProfile && world.hasCheckpoint) {
            return world.usesLegacyFallback() ? "Ready to resume (legacy path)" : "Ready to resume";
        }
        if (world.hasProfile) {
            return "Opens from profile only";
        }
        return "Checkpoint without profile";
    }

    [[nodiscard]] static ImVec4 worldStorageStatusColor(const StoredWorldInfo& world) {
        if (!world.hasProfile && !world.hasCheckpoint) {
            return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
        }
        if (world.hasProfile && world.hasCheckpoint && !world.usesLegacyFallback()) {
            return ImVec4(0.58f, 0.88f, 0.62f, 1.0f);
        }
        if (world.hasProfile && world.hasCheckpoint) {
            return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
        }
        if (world.hasProfile) {
            return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
        }
        return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
    }

    [[nodiscard]] static std::string worldResumeSummary(const StoredWorldInfo& world) {
        std::ostringstream out;
        if (world.hasCheckpoint) {
            out << "Open restores the checkpointed runtime state";
            if (world.stepIndex > 0) {
                out << " at step " << world.stepIndex;
            }
            if (world.hasCheckpointTimestamp) {
                out << " saved " << session_manager::formatFileTime(world.checkpointLastWrite, true);
            }
            out << ".";
            return out.str();
        }
        if (world.hasProfile) {
            out << "Open rebuilds the world from profile settings";
            if (world.hasProfileTimestamp) {
                out << " saved " << session_manager::formatFileTime(world.profileLastWrite, true);
            }
            out << ". No checkpointed runtime state is available.";
            return out.str();
        }
        return "This record is missing both profile and checkpoint data and may not open successfully.";
    }

    [[nodiscard]] static std::string worldPersistenceSummary(const StoredWorldInfo& world) {
        std::ostringstream out;
        out << (world.hasProfile ? "Profile saved" : "Profile missing")
            << " | "
            << (world.hasCheckpoint ? "Checkpoint saved" : "Checkpoint missing")
            << " | "
            << (world.hasDisplayPrefs ? "View layout saved" : "No saved view layout");
        return out.str();
    }

    [[nodiscard]] static std::string worldStorageScopeSummary(const StoredWorldInfo& world) {
        if (world.usesLegacyFallback()) {
            std::vector<std::string> fallbackParts;
            if (world.profileUsesFallback) {
                fallbackParts.push_back("profile");
            }
            if (world.checkpointUsesFallback) {
                fallbackParts.push_back("checkpoint");
            }
            if (world.displayPrefsUsesFallback) {
                fallbackParts.push_back("view layout");
            }

            std::ostringstream out;
            out << "Uses legacy fallback path for ";
            for (std::size_t i = 0; i < fallbackParts.size(); ++i) {
                if (i > 0) {
                    out << (i + 1 == fallbackParts.size() ? " and " : ", ");
                }
                out << fallbackParts[i];
            }
            out << ".";
            return out.str();
        }

        if (world.modelKey.empty() || world.modelKey == "default") {
            return "Stored in the default workspace scope.";
        }

        return "Stored under the active model scope.";
    }

    [[nodiscard]] static bool worldHasStorageIncomplete(const StoredWorldInfo& world) {
        return !(world.hasProfile && world.hasCheckpoint);
    }

    [[nodiscard]] static bool worldIsRecentlyActive(const StoredWorldInfo& world, const std::chrono::hours window = std::chrono::hours(72)) {
        bool hasAnyTimestamp = false;
        std::filesystem::file_time_type latest{};

        if (world.hasProfileTimestamp) {
            latest = world.profileLastWrite;
            hasAnyTimestamp = true;
        }
        if (world.hasCheckpointTimestamp && (!hasAnyTimestamp || world.checkpointLastWrite > latest)) {
            latest = world.checkpointLastWrite;
            hasAnyTimestamp = true;
        }
        if (world.hasDisplayPrefsTimestamp && (!hasAnyTimestamp || world.displayPrefsLastWrite > latest)) {
            latest = world.displayPrefsLastWrite;
            hasAnyTimestamp = true;
        }

        if (!hasAnyTimestamp) {
            return false;
        }

        const auto now = std::filesystem::file_time_type::clock::now();
        return latest + window >= now;
    }

    [[nodiscard]] static std::filesystem::path uniquePathWithCopySuffix(const std::filesystem::path& destination) {
        if (destination.empty() || !std::filesystem::exists(destination)) {
            return destination;
        }

        const std::filesystem::path parent = destination.parent_path();
        const std::string stem = destination.stem().string();
        const std::filesystem::path extension = destination.extension();
        int copyIndex = 1;
        for (;;) {
            const std::string suffix = copyIndex == 1 ? "_copy" : ("_copy_" + std::to_string(copyIndex));
            const std::filesystem::path candidate = parent / (stem + suffix + extension.string());
            if (!std::filesystem::exists(candidate)) {
                return candidate;
            }
            ++copyIndex;
        }
    }

    [[nodiscard]] static std::string translateSessionStatusMessage(const std::string& rawMessage) {
        if (rawMessage.empty()) {
            return {};
        }

        const std::string lower = app::toLower(rawMessage);

        if (lower.find("wizard_step_blocked reason=unresolved_bindings") != std::string::npos) {
            return "Cannot continue: the selected setup cannot write required model variables. Fix bindings or switch to Advanced mode to use expert override.";
        }
        if (lower.find("wizard_step_blocked reason=preflight_blocking") != std::string::npos) {
            return "Cannot continue: preflight found blocking issues that would produce an invalid or unsafe world.";
        }
        if (lower.find("world_create_blocked reason=verification_failed") != std::string::npos) {
            return "World creation blocked by preflight verification failures.";
        }
        if (lower.find("world_create_blocked reason=model_catalog_unavailable") != std::string::npos) {
            return "World creation blocked: model variable catalog is unavailable.";
        }
        if (lower.find("world_create_blocked reason=missing_conway_target_variable") != std::string::npos) {
            return "World creation blocked: Conway mode requires a target variable.";
        }
        if (lower.find("world_create_blocked reason=missing_gray_scott_target_variable") != std::string::npos) {
            return "World creation blocked: Gray-Scott mode requires both target variables.";
        }
        if (lower.find("world_create_blocked reason=missing_waves_target_variable") != std::string::npos) {
            return "World creation blocked: Waves mode requires a target variable.";
        }
        if (lower.find("world_create_blocked reason=unsupported_generation_mode") != std::string::npos) {
            return "World creation blocked: selected generation mode is not supported by the runtime.";
        }
        if (lower.find("world_create_blocked unresolved_bindings=") != std::string::npos) {
            return "World creation blocked: unresolved initialization bindings remain.";
        }
        if (lower.find("world_open_failed") != std::string::npos) {
            return "Failed to open the selected world. Check compatibility and file availability.";
        }
        if (lower.find("world_delete_failed") != std::string::npos) {
            return "Failed to delete the selected world.";
        }
        if (lower.find("world_rename_failed") != std::string::npos) {
            return "Failed to rename the selected world.";
        }
        if (lower.find("world_duplicate_failed") != std::string::npos) {
            return "Failed to duplicate the selected world.";
        }
        if (lower.find("world_export_failed") != std::string::npos) {
            return "Failed to export the selected world.";
        }
        if (lower.find("world_import_failed") != std::string::npos) {
            return "Failed to import world data from the selected file.";
        }

        return rawMessage;
    }

    void setSessionStatusText(const std::string& message) {
        std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
        sessionUi_.statusTechnicalDetail[0] = '\0';
    }

    void setSessionStatusFromRaw(const std::string& rawMessage) {
        const std::string translated = translateSessionStatusMessage(rawMessage);
        setSessionStatusText(translated);
        if (!rawMessage.empty() && rawMessage != translated) {
            std::snprintf(
                sessionUi_.statusTechnicalDetail,
                sizeof(sessionUi_.statusTechnicalDetail),
                "%s",
                rawMessage.c_str());
        }
    }

    void setSessionDataOperationReceipt(
        const std::string& operation,
        const DataOperationMode mode,
        const std::string& summary,
        const std::string& recoveryHint,
        const std::string& technicalDetail = {}) {
        const std::string receipt = dataOperationReceipt(operation, mode, summary, recoveryHint);
        std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", receipt.c_str());
        std::snprintf(sessionUi_.statusTechnicalDetail, sizeof(sessionUi_.statusTechnicalDetail), "%s", technicalDetail.c_str());
    }

    void tickDeferredWizardInitialization() {
        if (!deferredWizardInitialization_.active) {
            return;
        }

        const std::size_t total = deferredWizardInitialization_.pendingSettings.size();
        if (deferredWizardInitialization_.nextIndex < total) {
            const auto& setting = deferredWizardInitialization_.pendingSettings[deferredWizardInitialization_.nextIndex];
            const float restrictedValue = applyVariableRestriction(setting, setting.baseValue);
            std::string patchMessage;
            runtime_.applyManualPatch(
                setting.variableId,
                std::nullopt,
                restrictedValue,
                "wizard_variable_init",
                patchMessage);
            appendLog(patchMessage);
            ++deferredWizardInitialization_.appliedVariableCount;
            ++deferredWizardInitialization_.nextIndex;

            const float progress = total > 0u
                ? static_cast<float>(deferredWizardInitialization_.nextIndex) / static_cast<float>(total)
                : 1.0f;
            std::ostringstream detail;
            detail << "Applying variable initializers "
                   << deferredWizardInitialization_.nextIndex
                   << "/" << total;
            beginOperationStatus("world creation", progress, detail.str().c_str());
            return;
        }

        const float elapsedMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - deferredWizardInitialization_.startedAt)
                .count());
        {
            std::ostringstream perf;
            perf << "wizard_variable_init_complete vars=" << deferredWizardInitialization_.appliedVariableCount
                 << " estimated_writes=" << deferredWizardInitialization_.estimatedWrites
                 << " duration_ms=" << static_cast<int>(elapsedMs);
            appendLog(perf.str());
        }
        for (const auto& warning : deferredWizardInitialization_.verificationWarnings) {
            appendLog("verification_warning: " + warning);
        }

        completeOperationStatus(deferredWizardInitialization_.startedAt, "world creation complete");
        deferredWizardInitialization_ = DeferredWizardInitialization{};
        sessionUi_.needsRefresh = true;

        syncPanelFromConfig();
        refreshFieldNames();
        resetDisplayConfigToDefaults();
        loadDisplayPrefs();
        enterSimulationPaused();
    }

    void drawSessionManager() {
        if (sessionUi_.needsRefresh) {
            beginOperationStatus("refresh world list", 0.25f, "loading stored worlds");
            const auto refreshStartedAt = std::chrono::steady_clock::now();
            std::string listMessage;
            sessionUi_.worlds = runtime_.listStoredWorlds(listMessage);
            if (sessionUi_.selectedWorldIndex >= static_cast<int>(sessionUi_.worlds.size())) {
                sessionUi_.selectedWorldIndex = static_cast<int>(sessionUi_.worlds.empty() ? -1 : 0);
            }
            if (sessionUi_.selectedWorldIndex < 0 && !sessionUi_.worlds.empty()) {
                sessionUi_.selectedWorldIndex = 0;
            }
            if (sessionUi_.worlds.empty()) {
                setSessionStatusText("No saved worlds were found for the active model.");
            } else {
                setSessionStatusText(
                    std::string("Loaded ") + std::to_string(sessionUi_.worlds.size()) +
                    " saved world" + (sessionUi_.worlds.size() == 1u ? "" : "s") +
                    ". Select one to review how it will open.");
            }
            sessionUi_.needsRefresh = false;
            completeOperationStatus(refreshStartedAt, "world list refreshed");
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

        if (sessionUi_.operationLabel[0] != '\0' || sessionUi_.operationDetail[0] != '\0') {
            ImGui::BeginChild("SessionOperationStatus", ImVec2(-1.0f, 86.0f), true);
            ImGui::Text("Operation: %s", sessionUi_.operationLabel[0] != '\0' ? sessionUi_.operationLabel : "idle");
            if (sessionUi_.operationProgress >= 0.0f) {
                ImGui::ProgressBar(std::clamp(sessionUi_.operationProgress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextDisabled("%s", sessionUi_.operationDetail[0] != '\0' ? sessionUi_.operationDetail : "No recent operation details.");
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float actionBarH = 224.0f;
        const float contentH = std::max(220.0f, ImGui::GetContentRegionAvail().y - actionBarH - kS3);
        const bool narrowLayout = rootW < 1100.0f;

        std::vector<int> filteredWorldIndices;
        filteredWorldIndices.reserve(sessionUi_.worlds.size());
        static int worldCompareSelectionIndex = -1;
        const std::string query = app::toLower(std::string(sessionUi_.worldSearch));
        for (int i = 0; i < static_cast<int>(sessionUi_.worlds.size()); ++i) {
            const auto& world = sessionUi_.worlds[static_cast<std::size_t>(i)];
            if (sessionUi_.filterCheckpointOnly && !world.hasCheckpoint) {
                continue;
            }
            if (sessionUi_.filterProfileOnly && !world.hasProfile) {
                continue;
            }
            if (sessionUi_.filterResumeFromCheckpoint && !world.hasCheckpoint) {
                continue;
            }
            if (sessionUi_.filterStorageIncomplete && !worldHasStorageIncomplete(world)) {
                continue;
            }
            if (sessionUi_.filterRecentlyActive && !worldIsRecentlyActive(world)) {
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
                const std::uintmax_t bytesA = a.profileBytes + a.checkpointBytes + a.displayPrefsBytes;
                const std::uintmax_t bytesB = b.profileBytes + b.checkpointBytes + b.displayPrefsBytes;
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
        static constexpr const char* kSortModes[] = {"Name", "Recent", "Grid size", "Storage size"};
        const float worldFilterWidth = ImGui::GetContentRegionAvail().x;
        const bool compactWorldFilterLayout = worldFilterWidth < 640.0f;

        if (!compactWorldFilterLayout) {
            const float sortWidth = 130.0f;
            const float reserved = sortWidth + (3.0f * kS2) + 180.0f;
            ImGui::SetNextItemWidth(std::max(180.0f, worldFilterWidth - reserved));
            ImGui::InputTextWithHint("##world_search", "Search by name or mode", sessionUi_.worldSearch, IM_ARRAYSIZE(sessionUi_.worldSearch));
            ImGui::SameLine(0.0f, kS2);
            ImGui::SetNextItemWidth(sortWidth);
            ImGui::Combo("##world_sort", &sessionUi_.sortMode, kSortModes, static_cast<int>(std::size(kSortModes)));
            ImGui::SameLine(0.0f, kS2);
            ImGui::Checkbox("Checkpoint", &sessionUi_.filterCheckpointOnly);
            ImGui::SameLine(0.0f, kS2);
            ImGui::Checkbox("Profile", &sessionUi_.filterProfileOnly);
        } else {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##world_search", "Search by name or mode", sessionUi_.worldSearch, IM_ARRAYSIZE(sessionUi_.worldSearch));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo("##world_sort", &sessionUi_.sortMode, kSortModes, static_cast<int>(std::size(kSortModes)));
            const float togglesWidth = ImGui::GetContentRegionAvail().x;
            if (togglesWidth >= 300.0f) {
                ImGui::Checkbox("Checkpoint", &sessionUi_.filterCheckpointOnly);
                ImGui::SameLine(0.0f, kS2);
                ImGui::Checkbox("Profile", &sessionUi_.filterProfileOnly);
            } else {
                ImGui::Checkbox("Checkpoint", &sessionUi_.filterCheckpointOnly);
                ImGui::Checkbox("Profile", &sessionUi_.filterProfileOnly);
            }
        }
        DelayedTooltip("Filter worlds requiring checkpoint/profile presence.");
        ImGui::Spacing();
        ImGui::TextDisabled("Quick filters");
        const float quickFilterWidth = ImGui::GetContentRegionAvail().x;
        if (quickFilterWidth >= 560.0f) {
            ImGui::Checkbox("Resume now", &sessionUi_.filterResumeFromCheckpoint);
            DelayedTooltip("Show only worlds that can resume from a checkpoint immediately.");
            ImGui::SameLine(0.0f, kS2);
            ImGui::Checkbox("Storage incomplete", &sessionUi_.filterStorageIncomplete);
            DelayedTooltip("Show worlds missing either a profile or checkpoint artifact.");
            ImGui::SameLine(0.0f, kS2);
            ImGui::Checkbox("Recently active", &sessionUi_.filterRecentlyActive);
        } else {
            ImGui::Checkbox("Resume now", &sessionUi_.filterResumeFromCheckpoint);
            DelayedTooltip("Show only worlds that can resume from a checkpoint immediately.");
            ImGui::Checkbox("Storage incomplete", &sessionUi_.filterStorageIncomplete);
            DelayedTooltip("Show worlds missing either a profile or checkpoint artifact.");
            ImGui::Checkbox("Recently active", &sessionUi_.filterRecentlyActive);
        }
        DelayedTooltip("Show worlds updated within roughly the last 72 hours.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldList", ImVec2(-1.0f, narrowLayout ? 210.0f : -1.0f), true);
        if (filteredWorldIndices.empty()) {
            EmptyStateCard("No saved worlds found.", "Create a new world to generate your first simulation.");
        } else {
            for (int i = 0; i < static_cast<int>(filteredWorldIndices.size()); ++i) {
                const int worldIndex = filteredWorldIndices[static_cast<std::size_t>(i)];
                const auto& world = sessionUi_.worlds[static_cast<std::size_t>(worldIndex)];
                const bool selected = (sessionUi_.selectedWorldIndex == worldIndex);
                if (ImGui::Selectable(world.worldName.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 36.0f))) {
                    sessionUi_.selectedWorldIndex = worldIndex;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openSelectedWorld();
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                    sessionUi_.selectedWorldIndex = worldIndex;
                    ImGui::OpenPopup("World Actions Menu");
                }
                ImGui::SameLine();
                const std::string rowDetail =
                    std::string(world.initialConditionMode.empty() ? "n/a" : world.initialConditionMode.c_str()) +
                    " | " + worldStorageStatusLabel(world) +
                    " | " + (world.hasCheckpoint ? "restores checkpoint" : "rebuilds from profile");
                ImGui::TextDisabled("  %s", rowDetail.c_str());
            }
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("World details", "Review metadata, storage, and checkpoint status.");
        ImGui::Checkbox("Forensic detail mode", &sessionUi_.forensicDetailsMode);
        DelayedTooltip("Decision mode focuses on open/readiness cues. Enable forensic mode for full diagnostics.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldDetails", ImVec2(-1.0f, -1.0f), true);
        if (sessionUi_.selectedWorldIndex >= 0 && sessionUi_.selectedWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
            const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            if (worldCompareSelectionIndex == sessionUi_.selectedWorldIndex) {
                worldCompareSelectionIndex = -1;
            }
            if (worldCompareSelectionIndex >= static_cast<int>(sessionUi_.worlds.size())) {
                worldCompareSelectionIndex = -1;
            }
            const std::uint64_t gridCells = static_cast<std::uint64_t>(world.gridWidth) * static_cast<std::uint64_t>(world.gridHeight);
            const std::uintmax_t totalBytes = world.profileBytes + world.checkpointBytes + world.displayPrefsBytes;
            const ImVec4 statusColor = worldStorageStatusColor(world);
            const std::string resumeSummary = worldResumeSummary(world);
            const std::string persistenceSummary = worldPersistenceSummary(world);
            const std::string storageScopeSummary = worldStorageScopeSummary(world);
            ImGui::Text("Name: %s", world.worldName.c_str());
            ImGui::TextColored(statusColor, "%s", worldStorageStatusLabel(world));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextWrapped("%s", resumeSummary.c_str());
            ImGui::TextDisabled("%s", persistenceSummary.c_str());
            ImGui::PopTextWrapPos();

            if (!sessionUi_.forensicDetailsMode) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextDisabled("Decision summary");
                ImGui::BulletText("Open source: %s", world.hasCheckpoint ? "checkpoint resume" : "profile rebuild");
                ImGui::BulletText("Checkpoint step: %llu", static_cast<unsigned long long>(world.stepIndex));
                ImGui::BulletText("Grid: %ux%u", world.gridWidth, world.gridHeight);
                ImGui::BulletText("Storage integrity: %s", worldHasStorageIncomplete(world) ? "incomplete" : "complete");
                ImGui::BulletText("Activity: %s", worldIsRecentlyActive(world) ? "recent" : "older");
                ImGui::Spacing();
                ImGui::TextDisabled("Enable forensic detail mode for full paths, sizes, timestamps, and comparisons.");
            } else {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextDisabled("World profile");
                ImGui::Text("Grid: %ux%u", world.gridWidth, world.gridHeight);
                ImGui::Text("Cells: %llu", static_cast<unsigned long long>(gridCells));
                ImGui::Text("Seed: %llu", static_cast<unsigned long long>(world.seed));
                ImGui::Text("Temporal: %s", world.temporalPolicy.empty() ? "n/a" : world.temporalPolicy.c_str());
                ImGui::Text("Initialization: %s", world.initialConditionMode.empty() ? "n/a" : world.initialConditionMode.c_str());
                ImGui::Text("Data footprint: %s", session_manager::formatBytes(totalBytes).c_str());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextDisabled("Persistence details");
                ImGui::Text("Profile: %s", world.profilePath.string().c_str());
                ImGui::Text("Profile size: %s", session_manager::formatBytes(world.profileBytes).c_str());
                ImGui::Text("Profile updated: %s", session_manager::formatFileTime(world.profileLastWrite, world.hasProfileTimestamp).c_str());
                ImGui::Text("Checkpoint: %s", world.hasCheckpoint ? "Saved" : "Missing");
                if (world.hasCheckpoint) {
                    ImGui::Text("Last saved step: %llu", static_cast<unsigned long long>(world.stepIndex));
                    ImGui::Text("Run identity: %016llx", static_cast<unsigned long long>(world.runIdentityHash));
                    ImGui::Text("Checkpoint size: %s", session_manager::formatBytes(world.checkpointBytes).c_str());
                    ImGui::Text("Checkpoint updated: %s", session_manager::formatFileTime(world.checkpointLastWrite, world.hasCheckpointTimestamp).c_str());
                } else {
                    ImGui::TextDisabled("Open will start from the saved profile because no checkpoint is available.");
                }
                ImGui::Text("View layout: %s", world.hasDisplayPrefs ? "Saved" : "Not saved");
                if (world.hasDisplayPrefs) {
                    ImGui::Text("View layout size: %s", session_manager::formatBytes(world.displayPrefsBytes).c_str());
                    ImGui::Text("View layout updated: %s", session_manager::formatFileTime(world.displayPrefsLastWrite, world.hasDisplayPrefsTimestamp).c_str());
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextDisabled("Storage scope");
                ImGui::Text("Scope key: %s", world.modelKey.empty() ? "default" : world.modelKey.c_str());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", storageScopeSummary.c_str());
                ImGui::PopTextWrapPos();

                const char* compareLabel = "<none>";
                if (worldCompareSelectionIndex >= 0 && worldCompareSelectionIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    compareLabel = sessionUi_.worlds[static_cast<std::size_t>(worldCompareSelectionIndex)].worldName.c_str();
                }
                if (ImGui::BeginCombo("Compare against", compareLabel)) {
                    const bool noCompare = (worldCompareSelectionIndex < 0);
                    if (ImGui::Selectable("<none>", noCompare)) {
                        worldCompareSelectionIndex = -1;
                    }
                    if (noCompare) {
                        ImGui::SetItemDefaultFocus();
                    }

                    for (int i = 0; i < static_cast<int>(sessionUi_.worlds.size()); ++i) {
                        if (i == sessionUi_.selectedWorldIndex) {
                            continue;
                        }
                        const bool selected = (worldCompareSelectionIndex == i);
                        const auto& candidate = sessionUi_.worlds[static_cast<std::size_t>(i)];
                        if (ImGui::Selectable(candidate.worldName.c_str(), selected)) {
                            worldCompareSelectionIndex = i;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (worldCompareSelectionIndex >= 0 && worldCompareSelectionIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& other = sessionUi_.worlds[static_cast<std::size_t>(worldCompareSelectionIndex)];
                    const std::int64_t widthDelta = static_cast<std::int64_t>(world.gridWidth) - static_cast<std::int64_t>(other.gridWidth);
                    const std::int64_t heightDelta = static_cast<std::int64_t>(world.gridHeight) - static_cast<std::int64_t>(other.gridHeight);
                    const std::int64_t stepDelta = static_cast<std::int64_t>(world.stepIndex) - static_cast<std::int64_t>(other.stepIndex);
                    const std::int64_t bytesDelta = static_cast<std::int64_t>(totalBytes) -
                        static_cast<std::int64_t>(other.profileBytes + other.checkpointBytes + other.displayPrefsBytes);

                    ImGui::Text("Delta vs %s", other.worldName.c_str());
                    ImGui::BulletText("Grid delta: %+lld x %+lld", static_cast<long long>(widthDelta), static_cast<long long>(heightDelta));
                    ImGui::BulletText("Seed match: %s", world.seed == other.seed ? "yes" : "no");
                    ImGui::BulletText("Checkpoint step delta: %+lld", static_cast<long long>(stepDelta));
                    ImGui::BulletText("Data footprint delta: %+lld bytes", static_cast<long long>(bytesDelta));
                    ImGui::BulletText("Run identity match: %s", world.runIdentityHash == other.runIdentityHash ? "yes" : "no");
                    ImGui::BulletText("Profile size match: %s", world.profileBytes == other.profileBytes ? "yes" : "no");
                }
            }
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
        const float primaryBtnW = std::max(170.0f, (w - kS2) * 0.5f);
        const float secondaryBtnW = std::max(112.0f, (w - (4.0f * kS2)) / 5.0f);
        constexpr float kActionButtonH = 42.0f;

        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (PrimaryButton("Open world", ImVec2(primaryBtnW, kActionButtonH))) {
            openSelectedWorld();
        }
        DelayedTooltip("Restores the saved checkpoint when available; otherwise rebuilds the world from saved profile settings.");
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Create world", ImVec2(primaryBtnW, kActionButtonH))) {
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
        }
        DelayedTooltip("Open world creation with smart default naming based on the selected model.");

        ImGui::Spacing();

        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (SecondaryButton("Duplicate", ImVec2(secondaryBtnW, kActionButtonH))) {
            sessionUi_.pendingDuplicateWorldIndex = sessionUi_.selectedWorldIndex;
            const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            const std::string suggested = runtime_.suggestWorldNameFromHint(selected.worldName + "_copy");
            std::snprintf(sessionUi_.pendingDuplicateName, sizeof(sessionUi_.pendingDuplicateName), "%s", suggested.c_str());
            ImGui::OpenPopup("Duplicate World");
        }
        DelayedTooltip("Create a copied world with a new name.");

        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Rename", ImVec2(secondaryBtnW, kActionButtonH))) {
            sessionUi_.pendingRenameWorldIndex = sessionUi_.selectedWorldIndex;
            const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            std::snprintf(sessionUi_.pendingRenameName, sizeof(sessionUi_.pendingRenameName), "%s", selected.worldName.c_str());
            ImGui::OpenPopup("Rename World");
        }
        DelayedTooltip("Rename the selected world.");

        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Export", ImVec2(secondaryBtnW, kActionButtonH))) {
            sessionUi_.pendingExportWorldIndex = sessionUi_.selectedWorldIndex;
            const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            std::snprintf(sessionUi_.pendingExportPath, sizeof(sessionUi_.pendingExportPath), "exports/%s.wsexp", selected.worldName.c_str());
            ImGui::OpenPopup("Export World");
        }
        DelayedTooltip("Export selected world to a .wsexp file.");

        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Delete", ImVec2(secondaryBtnW, kActionButtonH))) {
            sessionUi_.pendingDeleteWorldIndex = sessionUi_.selectedWorldIndex;
            ImGui::OpenPopup("Delete World Confirm");
        }
        DelayedTooltip("Delete the selected world and its stored data.");
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine(0.0f, kS2);
        if (SecondaryButton("Import", ImVec2(secondaryBtnW, kActionButtonH))) {
            ImGui::OpenPopup("Import World");
        }
        DelayedTooltip("Import a world from a .wsexp file.");

        if (ImGui::BeginPopup("World Actions Menu")) {
            if (ImGui::MenuItem("Duplicate world")) {
                sessionUi_.pendingDuplicateWorldIndex = sessionUi_.selectedWorldIndex;
                if (canOpen) {
                    const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                    const std::string suggested = runtime_.suggestWorldNameFromHint(selected.worldName + "_copy");
                    std::snprintf(sessionUi_.pendingDuplicateName, sizeof(sessionUi_.pendingDuplicateName), "%s", suggested.c_str());
                }
                ImGui::OpenPopup("Duplicate World");
            }
            if (ImGui::MenuItem("Rename world")) {
                sessionUi_.pendingRenameWorldIndex = sessionUi_.selectedWorldIndex;
                if (canOpen) {
                    const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                    std::snprintf(sessionUi_.pendingRenameName, sizeof(sessionUi_.pendingRenameName), "%s", selected.worldName.c_str());
                }
                ImGui::OpenPopup("Rename World");
            }
            if (ImGui::MenuItem("Export world")) {
                sessionUi_.pendingExportWorldIndex = sessionUi_.selectedWorldIndex;
                if (canOpen) {
                    const auto& selected = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
                    std::snprintf(sessionUi_.pendingExportPath, sizeof(sessionUi_.pendingExportPath), "exports/%s.wsexp", selected.worldName.c_str());
                }
                ImGui::OpenPopup("Export World");
            }
            if (ImGui::MenuItem("Import world")) {
                ImGui::OpenPopup("Import World");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete world")) {
                sessionUi_.pendingDeleteWorldIndex = sessionUi_.selectedWorldIndex;
                ImGui::OpenPopup("Delete World Confirm");
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Duplicate World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("New world name", sessionUi_.pendingDuplicateName, IM_ARRAYSIZE(sessionUi_.pendingDuplicateName));
            const std::string requestedName = sessionUi_.pendingDuplicateName;
            const std::string normalizedName = runtime_.normalizeWorldNameForUi(requestedName);
            const bool duplicateNameValid = !normalizedName.empty();
            if (!duplicateNameValid) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Enter a valid world name.");
            } else if (normalizedName != requestedName) {
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Normalized name: %s", normalizedName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Name format: valid");
            }

            if (!duplicateNameValid) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton("Duplicate", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingDuplicateWorldIndex >= 0 && sessionUi_.pendingDuplicateWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingDuplicateWorldIndex)];
                    std::string message;
                    beginOperationStatus("duplicate world", 0.4f, world.worldName.c_str());
                    const auto startedAt = std::chrono::steady_clock::now();
                    if (requestedName != normalizedName) {
                        std::snprintf(sessionUi_.pendingDuplicateName, sizeof(sessionUi_.pendingDuplicateName), "%s", normalizedName.c_str());
                    }
                    const bool duplicated = runtime_.duplicateWorld(world.worldName, normalizedName, message);
                    completeOperationStatus(startedAt, "duplicate world finished");
                    if (duplicated) {
                        setSessionStatusText(std::string("Duplicated '") + world.worldName + "' as '" + normalizedName + "'.");
                    } else {
                        setSessionStatusFromRaw(message);
                    }
                    appendLog(message);
                    sessionUi_.needsRefresh = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (!duplicateNameValid) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Rename World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("World name", sessionUi_.pendingRenameName, IM_ARRAYSIZE(sessionUi_.pendingRenameName));
            const std::string requestedName = sessionUi_.pendingRenameName;
            const std::string normalizedName = runtime_.normalizeWorldNameForUi(requestedName);
            const bool renameNameValid = !normalizedName.empty();
            if (!renameNameValid) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Enter a valid world name.");
            } else if (normalizedName != requestedName) {
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Normalized name: %s", normalizedName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Name format: valid");
            }

            if (!renameNameValid) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton("Rename", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingRenameWorldIndex >= 0 && sessionUi_.pendingRenameWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingRenameWorldIndex)];
                    std::string message;
                    beginOperationStatus("rename world", 0.4f, world.worldName.c_str());
                    const auto startedAt = std::chrono::steady_clock::now();
                    if (requestedName != normalizedName) {
                        std::snprintf(sessionUi_.pendingRenameName, sizeof(sessionUi_.pendingRenameName), "%s", normalizedName.c_str());
                    }
                    const bool renamed = runtime_.renameWorld(world.worldName, normalizedName, message);
                    completeOperationStatus(startedAt, "rename world finished");
                    if (renamed) {
                        setSessionStatusText(std::string("Renamed world to '") + normalizedName + "'.");
                    } else {
                        setSessionStatusFromRaw(message);
                    }
                    appendLog(message);
                    sessionUi_.needsRefresh = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (!renameNameValid) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Export World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int exportOperationModeIndex = static_cast<int>(DataOperationMode::Copy);
            ImGui::InputText("Export path", sessionUi_.pendingExportPath, IM_ARRAYSIZE(sessionUi_.pendingExportPath));
            ImGui::SameLine();
            if (SecondaryButton("Browse...", ImVec2(110.0f, 0.0f))) {
                const std::filesystem::path defaultExportPath = std::filesystem::path(sessionUi_.pendingExportPath);
                if (const auto exportPath = pickNativeFilePath(
                        L"Export World",
                        L"World Export (*.wsexp)\0*.wsexp\0All Files (*.*)\0*.*\0\0",
                        defaultExportPath,
                        true)) {
                    std::snprintf(sessionUi_.pendingExportPath, sizeof(sessionUi_.pendingExportPath), "%s", exportPath->string().c_str());
                }
            }

            static constexpr const char* kModes[] = {"Copy", "Replace", "Merge"};
            exportOperationModeIndex = std::clamp(exportOperationModeIndex, 0, 2);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo("Operation mode", &exportOperationModeIndex, kModes, static_cast<int>(std::size(kModes)));
            const DataOperationMode exportMode = static_cast<DataOperationMode>(exportOperationModeIndex);
            const bool mergeSupported = false;

            const std::string exportPath = sessionUi_.pendingExportPath;
            const bool exportPathValid = !exportPath.empty();
            std::filesystem::path exportTarget = std::filesystem::path(exportPath);
            if (exportTarget.extension() != ".wsexp") {
                exportTarget += ".wsexp";
            }
            const bool exportTargetExists = exportPathValid && std::filesystem::exists(exportTarget);
            const bool dryRunValid = exportPathValid && (exportMode != DataOperationMode::Merge || mergeSupported);

            if (sessionUi_.pendingExportWorldIndex >= 0 && sessionUi_.pendingExportWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingExportWorldIndex)];
                ImGui::TextDisabled(
                    "Export copies the saved profile%s into a portable world file.",
                    world.hasCheckpoint ? " and checkpoint" : "");
            }
            ImGui::TextDisabled("Preflight summary");
            ImGui::TextWrapped("Source world: %s",
                (sessionUi_.pendingExportWorldIndex >= 0 && sessionUi_.pendingExportWorldIndex < static_cast<int>(sessionUi_.worlds.size()))
                    ? sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingExportWorldIndex)].worldName.c_str()
                    : "<none selected>");
            ImGui::TextWrapped("Target: %s", exportPathValid ? exportTarget.string().c_str() : "<missing>");
            ImGui::TextWrapped("Impact: %s", dataOperationOverwriteImpact(exportMode, exportTargetExists).c_str());
            ImGui::TextWrapped("Mode behavior: %s", dataOperationModeBehavior(exportMode));

            if (!exportPathValid) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Export path is required.");
            } else {
                const std::filesystem::path normalizedExportPath(exportPath);
                if (normalizedExportPath.extension() != ".wsexp") {
                    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Recommended extension: .wsexp");
                } else {
                    ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Export target looks valid.");
                }
            }

            if (!dryRunValid) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                    "Dry-run validation: failed (merge mode is not supported for world export).");
            } else {
                ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Dry-run validation: passed");
            }

            if (!dryRunValid) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton("Export", ImVec2(140.0f, 28.0f))) {
                if (sessionUi_.pendingExportWorldIndex >= 0 && sessionUi_.pendingExportWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
                    const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.pendingExportWorldIndex)];
                    std::string message;
                    beginOperationStatus("export world", 0.4f, world.worldName.c_str());
                    const auto startedAt = std::chrono::steady_clock::now();
                    std::filesystem::path effectiveExportPath = exportTarget;
                    if (exportMode == DataOperationMode::Copy && exportTargetExists) {
                        effectiveExportPath = uniquePathWithCopySuffix(exportTarget);
                    }
                    const bool exported = runtime_.exportWorld(world.worldName, effectiveExportPath, message);
                    completeOperationStatus(startedAt, "export world finished");
                    if (exported) {
                        setSessionDataOperationReceipt(
                            "World export",
                            exportMode,
                            std::string("Committed to '") + effectiveExportPath.string() + "'.",
                            "If this export target is wrong, rerun export with Copy mode to preserve existing files.",
                            message);
                    } else {
                        setSessionStatusFromRaw(message);
                    }
                    appendLog(message);
                }
                ImGui::CloseCurrentPopup();
            }
            if (!dryRunValid) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (SecondaryButton("Cancel", ImVec2(100.0f, 28.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Import World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int importOperationModeIndex = static_cast<int>(DataOperationMode::Copy);
            ImGui::InputText("Import path", sessionUi_.pendingImportPath, IM_ARRAYSIZE(sessionUi_.pendingImportPath));
            ImGui::SameLine();
            if (SecondaryButton("Browse...", ImVec2(110.0f, 0.0f))) {
                const std::filesystem::path defaultImportPath = std::filesystem::path(sessionUi_.pendingImportPath);
                if (const auto importPath = pickNativeFilePath(
                        L"Import World",
                        L"World Export (*.wsexp)\0*.wsexp\0All Files (*.*)\0*.*\0\0",
                        defaultImportPath,
                        false)) {
                    std::snprintf(sessionUi_.pendingImportPath, sizeof(sessionUi_.pendingImportPath), "%s", importPath->string().c_str());
                }
            }

            static constexpr const char* kModes[] = {"Copy", "Replace", "Merge"};
            importOperationModeIndex = std::clamp(importOperationModeIndex, 0, 2);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo("Operation mode", &importOperationModeIndex, kModes, static_cast<int>(std::size(kModes)));
            const DataOperationMode importMode = static_cast<DataOperationMode>(importOperationModeIndex);
            const bool modeSupported = importMode == DataOperationMode::Copy;

            const std::filesystem::path importPath = std::filesystem::path(sessionUi_.pendingImportPath);
            const bool importPathProvided = !importPath.empty();
            const bool importPathExists = importPathProvided && std::filesystem::exists(importPath);
            const bool dryRunValid = importPathExists && modeSupported;
            ImGui::TextDisabled("Imports always create a stored copy. If the incoming world name already exists, a numbered copy is created instead.");
            ImGui::TextDisabled("Preflight summary");
            ImGui::TextWrapped("Source: %s", importPathProvided ? importPath.string().c_str() : "<missing>");
            ImGui::TextWrapped("Target scope: active world store (%s)", runtime_.activeModelKey().c_str());
            ImGui::TextWrapped("Impact: %s", dataOperationOverwriteImpact(importMode, true).c_str());
            ImGui::TextWrapped("Mode behavior: %s", dataOperationModeBehavior(importMode));
            if (!importPathProvided) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Import file path is required.");
            } else if (!importPathExists) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Import file was not found.");
            } else {
                ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Import source detected.");
            }

            if (!modeSupported) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                    "Dry-run validation: failed (world import currently supports Copy mode only).");
            } else if (importPathExists) {
                ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Dry-run validation: passed");
            }

            if (!dryRunValid) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton("Import", ImVec2(140.0f, 28.0f))) {
                std::string importedName;
                std::string message;
                beginOperationStatus("import world", 0.4f, sessionUi_.pendingImportPath);
                const auto startedAt = std::chrono::steady_clock::now();
                const bool imported = runtime_.importWorld(std::filesystem::path(sessionUi_.pendingImportPath), importedName, message);
                completeOperationStatus(startedAt, "import world finished");
                if (imported) {
                    setSessionDataOperationReceipt(
                        "World import",
                        importMode,
                        std::string("Imported as '") + importedName + "'.",
                        "Review storage integrity, then open the imported world from the list.",
                        message);
                } else {
                    setSessionStatusFromRaw(message);
                }
                appendLog(message);
                sessionUi_.needsRefresh = true;
                ImGui::CloseCurrentPopup();
            }
            if (!dryRunValid) {
                ImGui::EndDisabled();
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
                    beginOperationStatus("delete world", 0.4f, world.worldName.c_str());
                    const auto startedAt = std::chrono::steady_clock::now();
                    const bool deleted = runtime_.deleteWorld(world.worldName, message);
                    completeOperationStatus(startedAt, "delete world finished");
                    appendLog(message);
                    if (deleted) {
                        setSessionStatusText(std::string("Deleted world '") + world.worldName + "'.");
                    } else {
                        setSessionStatusFromRaw(message);
                    }
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
        } else {
            const auto& selectedWorld = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            LabeledHint(worldResumeSummary(selectedWorld).c_str());
        }
        if (sessionUi_.statusMessage[0] != '\0') {
            LabeledHint(sessionUi_.statusMessage);
            if (sessionUi_.statusTechnicalDetail[0] != '\0') {
                ImGui::TextDisabled("Technical detail available. Hover for raw reason code.");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", sessionUi_.statusTechnicalDetail);
                }
            }
        }

        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();
    }

    void drawNewWorldWizard() {
        tickDeferredWizardInitialization();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("New World Wizard", nullptr, flags);

        const float rootW = viewport->Size.x;
        const float rootH = viewport->Size.y;
        const ImVec2 rootPos(0.0f, 0.0f);

        ImGui::SetCursorPos(rootPos);
        ImGui::BeginChild("WizardRoot", ImVec2(rootW, rootH), true);

        ImGui::BeginChild("WizardHeader", ImVec2(-1.0f, 88.0f), false);
        SectionHeader("Create New World", "Configure generation parameters before simulation start.");
        static constexpr std::array<const char*, 4> kWizardSteps = {
            "Basics",
            "Bindings",
            "Preflight",
            "Review & Create"};
        sessionUi_.wizardStepIndex = std::clamp(sessionUi_.wizardStepIndex, 0, static_cast<int>(kWizardSteps.size()) - 1);
        ImGui::TextDisabled(
            "Step %d/%d: %s",
            sessionUi_.wizardStepIndex + 1,
            static_cast<int>(kWizardSteps.size()),
            kWizardSteps[static_cast<std::size_t>(sessionUi_.wizardStepIndex)]);
        ImGui::ProgressBar(
            static_cast<float>(sessionUi_.wizardStepIndex + 1) / static_cast<float>(kWizardSteps.size()),
            ImVec2(-1.0f, 0.0f));
        ImGui::Checkbox("Advanced wizard mode", &sessionUi_.wizardAdvancedMode);
        DelayedTooltip("Standard mode keeps only common setup decisions visible. Advanced mode exposes binding and workload override controls.");
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

        if (sessionUi_.operationLabel[0] != '\0') {
            ImGui::BeginChild("WizardOperationStatus", ImVec2(-1.0f, 58.0f), true);
            ImGui::Text("Operation: %s", sessionUi_.operationLabel);
            if (sessionUi_.operationProgress >= 0.0f) {
                ImGui::ProgressBar(std::clamp(sessionUi_.operationProgress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            }
            ImGui::TextDisabled("%s", sessionUi_.operationDetail[0] != '\0' ? sessionUi_.operationDetail : "");
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

        if (sessionUi_.wizardStepIndex == 0) {
            SectionHeader("Generation parameters", "Define initial conditions and model setup.");
            ImGui::Spacing();
            ImGui::BeginChild("WizardForm", ImVec2(-1.0f, narrowLayout ? 320.0f : -1.0f), true);
            inputTextWithHint("World Name", sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "Name for this world. Letters, numbers, '_' and '-' are recommended.");
            {
                const std::string normalizedName = runtime_.normalizeWorldNameForUi(sessionUi_.pendingWorldName);
                if (normalizedName.empty()) {
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                        "World name will be auto-generated because current input is invalid or empty.");
                } else if (normalizedName != sessionUi_.pendingWorldName) {
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.80f, 0.45f, 1.0f),
                        "Normalized world name: %s",
                        normalizedName.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "World name format: valid");
                }
            }
            LabeledHint("Smart naming uses model context and avoids collisions automatically.");

            if (!sessionUi_.wizardAdvancedMode) {
                const auto recommendation = GenerationAdvisor::recommendGenerationMode(sessionUi_.selectedModelCatalog, {});
                const InitialConditionType recommendedMode = fallbackRuntimeSupportedMode(
                    refineRecommendedModeForKnownModels(sessionUi_.selectedModelCatalog, recommendation.recommendedType));
                const InitialConditionType selectedMode = static_cast<InitialConditionType>(
                    std::clamp(panel_.initialConditionTypeIndex, 0, static_cast<int>(InitialConditionType::DiffusionLimit)));
                const std::string normalizedWorldName = runtime_.normalizeWorldNameForUi(sessionUi_.pendingWorldName);

                ImGui::Spacing();
                ImGui::BeginChild("WizardGoalSummary", ImVec2(-1.0f, 96.0f), true);
                ImGui::TextDisabled("Step 1 goals");
                ImGui::BulletText("Confirm world identity: %s", normalizedWorldName.empty() ? "auto-name on create" : normalizedWorldName.c_str());
                ImGui::BulletText("Recommended generation mode: %s", generationModeLabel(recommendedMode));
                ImGui::BulletText("Current mode: %s", generationModeLabel(selectedMode));
                ImGui::EndChild();
            }

            ImGui::Spacing();
            drawGridSetupSection();
            ImGui::Spacing();
            drawWorldGenerationSection();
        } else {
            SectionHeader("Step guidance", "Follow the stepper to complete world creation.");
            ImGui::BeginChild("WizardGuidance", ImVec2(-1.0f, 88.0f), true);
            ImGui::TextWrapped("Current step: %s", kWizardSteps[static_cast<std::size_t>(sessionUi_.wizardStepIndex)]);
            ImGui::TextDisabled("Use Back/Next below. Generation parameters are edited in Step 1.");
            ImGui::EndChild();
            ImGui::BeginChild("WizardForm", ImVec2(-1.0f, narrowLayout ? 320.0f : -1.0f), true);
        }

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
        if (sessionUi_.wizardStepIndex == 1) {
            SectionHeader("Initialization binding preview", "Validate model-variable bindings before world creation.");
            if (!sessionUi_.wizardAdvancedMode) {
                if (!sessionUi_.generationBindingPlan.hasBlockingIssues()) {
                    ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Binding status: ready");
                    ImGui::TextWrapped("You're ready for the next step. The selected setup can write required model variables.");
                } else {
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                        "Binding status: action required (%d issue%s)",
                        static_cast<int>(sessionUi_.generationBindingPlan.issues.size()),
                        sessionUi_.generationBindingPlan.issues.size() == 1u ? "" : "s");
                    ImGui::TextWrapped("Fix the items below, then continue.");
                    for (const auto& issue : sessionUi_.generationBindingPlan.issues) {
                        std::string action = "Choose a generation mode that matches available model variables.";
                        const std::string codeLower = app::toLower(issue.code);
                        if (codeLower.find("conway") != std::string::npos) {
                            action = "Pick a Conway target variable in Step 1 generation settings.";
                        } else if (codeLower.find("grayscott") != std::string::npos || codeLower.find("gray_scott") != std::string::npos) {
                            action = "Set both Gray-Scott target variables in Step 1 generation settings.";
                        } else if (codeLower.find("waves") != std::string::npos) {
                            action = "Pick a Waves target variable in Step 1 generation settings.";
                        }
                        ImGui::BulletText("%s", action.c_str());
                    }

                    if (ImGui::CollapsingHeader("Diagnostics (advanced)")) {
                        ImGui::TextDisabled("Binding issues");
                        for (const auto& issue : sessionUi_.generationBindingPlan.issues) {
                            ImGui::BulletText("%s: %s", issue.code.c_str(), issue.message.c_str());
                        }
                    }
                }
            } else {
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
            }
        } else {
            ImGui::TextDisabled(
                "Binding status: %s",
                sessionUi_.generationBindingPlan.hasBlockingIssues() ? "unresolved" : "ready");
        }

        struct VerificationReport {
            std::vector<std::string> blocking;
            std::vector<std::string> warnings;
            int enabledVariableInitializers = 0;
            std::uint64_t estimatedGlobalWrites = 0;
        };

        const auto collectVerificationReport = [&]() {
            VerificationReport report;
            const InitialConditionType selectedType = static_cast<InitialConditionType>(
                std::clamp(panel_.initialConditionTypeIndex, 0, static_cast<int>(InitialConditionType::DiffusionLimit)));

            if (sessionUi_.selectedModelCatalog.variables.empty()) {
                report.blocking.push_back("Model variable catalog is unavailable.");
            }
            if (!isRuntimeSupportedGenerationMode(selectedType)) {
                report.blocking.push_back("Selected generation mode is not yet supported by runtime initialization.");
            }
            if (!sessionUi_.allowUnresolvedGenerationBindings && sessionUi_.generationBindingPlan.hasBlockingIssues()) {
                report.blocking.push_back("Required target variables are missing for the selected generation setup.");
            }

            if (selectedType == InitialConditionType::Conway && std::strlen(panel_.conwayTargetVariable) == 0u) {
                report.blocking.push_back("Conway target variable is required.");
            }
            if (selectedType == InitialConditionType::GrayScott &&
                (std::strlen(panel_.grayScottTargetVariableA) == 0u || std::strlen(panel_.grayScottTargetVariableB) == 0u)) {
                report.blocking.push_back("Gray-Scott requires both target variables.");
            }
            if (selectedType == InitialConditionType::GrayScott &&
                std::string(panel_.grayScottTargetVariableA) == std::string(panel_.grayScottTargetVariableB) &&
                std::strlen(panel_.grayScottTargetVariableA) > 0u) {
                report.blocking.push_back("Gray-Scott target A/B must be distinct.");
            }
            if (selectedType == InitialConditionType::Waves && std::strlen(panel_.wavesTargetVariable) == 0u) {
                report.blocking.push_back("Waves target variable is required.");
            }

            std::vector<std::string> seenVariableIds;
            seenVariableIds.reserve(sessionUi_.variableInitializationSettings.size());
            for (const auto& setting : sessionUi_.variableInitializationSettings) {
                if (!setting.enabled) {
                    continue;
                }
                ++report.enabledVariableInitializers;
                if (setting.variableId.empty()) {
                    report.blocking.push_back("An enabled variable initializer is missing its target variable id.");
                }
                if (!std::isfinite(setting.baseValue)) {
                    report.blocking.push_back("An enabled variable initializer has an invalid numeric base value.");
                }
                if (setting.restrictionMode == 1 && setting.clampMin > setting.clampMax) {
                    report.warnings.push_back("Clamp bounds are inverted for one or more variable initializers (auto-fix available).");
                }
                if (std::find(seenVariableIds.begin(), seenVariableIds.end(), setting.variableId) != seenVariableIds.end()) {
                    report.warnings.push_back("Duplicate enabled variable initializer target ids detected.");
                } else {
                    seenVariableIds.push_back(setting.variableId);
                }
            }

            const std::uint64_t cellCount =
                static_cast<std::uint64_t>(std::max(1, panel_.gridWidth)) *
                static_cast<std::uint64_t>(std::max(1, panel_.gridHeight));
            report.estimatedGlobalWrites =
                cellCount * static_cast<std::uint64_t>(std::max(0, report.enabledVariableInitializers));

            static constexpr std::uint64_t kWarnWrites = 2'000'000ull;
            static constexpr std::uint64_t kBlockWrites = 8'000'000ull;
            if (report.estimatedGlobalWrites >= kWarnWrites) {
                std::ostringstream warn;
                warn << "Large initialization workload detected (" << report.estimatedGlobalWrites
                     << " cell writes).";
                report.warnings.push_back(warn.str());
            }
            if (report.estimatedGlobalWrites >= kBlockWrites && !sessionUi_.allowHeavyInitializationWork) {
                std::ostringstream block;
                block << "Initialization workload exceeds safe interactive threshold (" << report.estimatedGlobalWrites
                      << " writes). Use Advanced wizard mode if this workload is intentional.";
                report.blocking.push_back(block.str());
            }

            if (report.enabledVariableInitializers == 0) {
                report.warnings.push_back("No optional variable initializers are enabled.");
            }

            if (sessionUi_.generationPreviewSourceIndex == 6 && sessionUi_.selectedModelCellStateVariables.empty()) {
                report.warnings.push_back("Preview source is set to a model variable channel, but no model variables are available.");
            }

            return report;
        };

        VerificationReport verification = collectVerificationReport();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (sessionUi_.wizardStepIndex == 2) {
            SectionHeader("Preflight verification", "Full readiness checks before world creation.");
            if (sessionUi_.wizardAdvancedMode) {
                ImGui::TextDisabled(
                    "Checks: %d blocking, %d warning, %d enabled x_i initializers, %llu estimated x_i writes",
                    static_cast<int>(verification.blocking.size()),
                    static_cast<int>(verification.warnings.size()),
                    verification.enabledVariableInitializers,
                    static_cast<unsigned long long>(verification.estimatedGlobalWrites));
            } else {
                ImGui::TextDisabled("Checks: %d blocking, %d recommendation%s",
                    static_cast<int>(verification.blocking.size()),
                    static_cast<int>(verification.warnings.size()),
                    verification.warnings.size() == 1u ? "" : "s");
            }

            const auto friendlyVerificationMessage = [&](const std::string& raw) {
                std::string text = raw;
                auto replaceAll = [&](const std::string& from, const std::string& to) {
                    std::size_t pos = 0;
                    while ((pos = text.find(from, pos)) != std::string::npos) {
                        text.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                };
                replaceAll("x_i", "model variable");
                replaceAll("initializer", "initialization setting");
                replaceAll("binding plan", "generation setup");
                return text;
            };

            if (sessionUi_.wizardAdvancedMode) {
                checkboxWithHint(
                    "Expert override heavy initialization workload",
                    &sessionUi_.allowHeavyInitializationWork,
                    "Allows creating worlds with very large x_i initialization write counts. Use only when expected; may increase create-time stall risk.");
            } else if (verification.estimatedGlobalWrites >= 8'000'000ull) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                    "This setup is too large for Standard mode safeguards.");
                ImGui::TextDisabled("Switch to Advanced wizard mode only if this workload is intentional.");
            }

            if (!sessionUi_.wizardAdvancedMode) {
                if (verification.blocking.empty()) {
                    ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Must fix before create: none");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Must fix before create:");
                    for (const auto& message : verification.blocking) {
                        const std::string friendly = friendlyVerificationMessage(message);
                        ImGui::BulletText("Do this: %s", friendly.c_str());
                    }
                }

                if (verification.warnings.empty()) {
                    ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Recommended to review: none");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Recommended to review:");
                    for (const auto& message : verification.warnings) {
                        const std::string friendly = friendlyVerificationMessage(message);
                        ImGui::BulletText("%s", friendly.c_str());
                    }
                }
                if (ImGui::CollapsingHeader("Diagnostics (advanced)")) {
                    ImGui::TextDisabled("Binding diagnostics");
                    if (sessionUi_.generationBindingPlan.issues.empty()) {
                        ImGui::TextDisabled("No unresolved binding issues.");
                    } else {
                        for (const auto& issue : sessionUi_.generationBindingPlan.issues) {
                            ImGui::BulletText("%s: %s", issue.code.c_str(), issue.message.c_str());
                        }
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Preflight technical diagnostics");
                    for (const auto& message : verification.blocking) {
                        ImGui::BulletText("blocking: %s", message.c_str());
                    }
                    for (const auto& message : verification.warnings) {
                        ImGui::BulletText("warning: %s", message.c_str());
                    }
                }
            } else {
                if (verification.blocking.empty()) {
                    ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.62f, 1.0f), "Blocking checks: PASS");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Blocking checks: FAIL");
                    for (const auto& message : verification.blocking) {
                        ImGui::BulletText("%s", message.c_str());
                    }
                }

                if (verification.warnings.empty()) {
                    ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Warnings: none");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Warnings:");
                    for (const auto& message : verification.warnings) {
                        ImGui::BulletText("%s", message.c_str());
                    }
                }
            }

            if (SecondaryButton("Auto-fix minor issues", ImVec2(190.0f, 22.0f))) {
                for (auto& setting : sessionUi_.variableInitializationSettings) {
                    if (setting.restrictionMode == 1 && setting.clampMin > setting.clampMax) {
                        std::swap(setting.clampMin, setting.clampMax);
                    }
                    if (!std::isfinite(setting.baseValue)) {
                        setting.baseValue = 0.0f;
                    }
                }
                verification = collectVerificationReport();
                appendLog("wizard_preflight_autofix_applied");
            }
        } else if (sessionUi_.wizardStepIndex == 3) {
            SectionHeader("Review", "Final checklist before creating the world.");
            ImGui::Text("World name: %s", sessionUi_.pendingWorldName[0] != '\0' ? sessionUi_.pendingWorldName : "<auto>");
            ImGui::Text("Grid: %dx%d", panel_.gridWidth, panel_.gridHeight);
            ImGui::Text("Generation mode: %s", generationModeLabel(static_cast<InitialConditionType>(panel_.initialConditionTypeIndex)));
            ImGui::Text("Wizard depth: %s", sessionUi_.wizardAdvancedMode ? "Advanced" : "Standard");
            if (sessionUi_.wizardAdvancedMode) {
                ImGui::Text("Bindings: %s", sessionUi_.generationBindingPlan.hasBlockingIssues() ? "needs attention" : "ready");
                ImGui::Text(
                    "Preflight: %d blocking, %d warnings",
                    static_cast<int>(verification.blocking.size()),
                    static_cast<int>(verification.warnings.size()));
            } else {
                ImGui::Text("Create readiness: %s", verification.blocking.empty() ? "ready" : "action required");
            }
            if (!verification.blocking.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "Resolve blocking items before creation.");
            }
        } else {
            ImGui::TextDisabled("Preflight checks are available in Step 3.");
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("Generation preview", "Preview only - simulation is not running.");
        ImGui::Spacing();
        ImGui::BeginChild("WizardPreview", ImVec2(-1.0f, -1.0f), true);
        static constexpr const char* kPreviewDisplayTypes[] = {
            "Scalar Field",
            "Surface Category",
            "Relative Elevation",
            "Surface Water",
            "Moisture Map",
            "Wind Field"};
        static constexpr const char* kPreviewSources[] = {
            "Auto (mode-driven)",
            "Primary signal",
            "Terrain proxy",
            "Water proxy",
            "Moisture proxy",
            "Wind magnitude proxy",
            "Model variable channel"};

        int previewDisplayTypeIndex = static_cast<int>(viz_.generationPreviewDisplayType);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(
            "Preview display",
            &previewDisplayTypeIndex,
            kPreviewDisplayTypes,
            static_cast<int>(std::size(kPreviewDisplayTypes)))) {
            viz_.generationPreviewDisplayType = static_cast<DisplayType>(std::clamp(previewDisplayTypeIndex, 0, 5));
        }

        sessionUi_.generationPreviewSourceIndex = std::clamp(sessionUi_.generationPreviewSourceIndex, 0, 6);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo(
            "Preview source",
            &sessionUi_.generationPreviewSourceIndex,
            kPreviewSources,
            static_cast<int>(std::size(kPreviewSources)));

        if (!sessionUi_.selectedModelCellStateVariables.empty()) {
            sessionUi_.generationPreviewChannelIndex = std::clamp(
                sessionUi_.generationPreviewChannelIndex,
                0,
                static_cast<int>(sessionUi_.selectedModelCellStateVariables.size()) - 1);
            const std::string& channelName = sessionUi_.selectedModelCellStateVariables[
                static_cast<std::size_t>(sessionUi_.generationPreviewChannelIndex)];
            if (ImGui::BeginCombo("Preview variable", channelName.c_str())) {
                for (int i = 0; i < static_cast<int>(sessionUi_.selectedModelCellStateVariables.size()); ++i) {
                    const bool selected = (i == sessionUi_.generationPreviewChannelIndex);
                    if (ImGui::Selectable(sessionUi_.selectedModelCellStateVariables[static_cast<std::size_t>(i)].c_str(), selected)) {
                        sessionUi_.generationPreviewChannelIndex = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();

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

        const ViewportConfig previewViewportConfig = [&]() {
            if (viz_.viewports.empty()) {
                return VisualizationState::makeDefaultViewportConfig(0);
            }
            const int maxViewportIndex = static_cast<int>(viz_.viewports.size()) - 1;
            const std::size_t idx = static_cast<std::size_t>(std::clamp(viz_.activeViewportEditor, 0, maxViewportIndex));
            return viz_.viewports[idx];
        }();

        std::uint64_t previewHash = 1469598103934665603ull;
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.seed));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.initialConditionTypeIndex));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(sessionUi_.generationPreviewSourceIndex));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(std::max(0, sessionUi_.generationPreviewChannelIndex)));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewBaseW));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewBaseH));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewStride));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(viz_.generationPreviewDisplayType));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(previewViewportConfig.displayManager.autoWaterLevel));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.waterLevel));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.autoWaterQuantile));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.lowlandThreshold));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.highlandThreshold));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.waterPresenceThreshold));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.shallowWaterDepth));
        previewHash = hashCombine(previewHash, hashFloat(previewViewportConfig.displayManager.highMoistureThreshold));
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
        previewHash = hashCombine(previewHash, hashFloat(panel_.conwayAliveProbability));
        previewHash = hashCombine(previewHash, hashFloat(panel_.conwayAliveValue));
        previewHash = hashCombine(previewHash, hashFloat(panel_.conwayDeadValue));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.conwaySmoothingPasses));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottBackgroundA));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottBackgroundB));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottSpotValueA));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottSpotValueB));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.grayScottSpotCount));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottSpotRadius));
        previewHash = hashCombine(previewHash, hashFloat(panel_.grayScottSpotJitter));
        previewHash = hashCombine(previewHash, hashFloat(panel_.waveBaseline));
        previewHash = hashCombine(previewHash, hashFloat(panel_.waveDropAmplitude));
        previewHash = hashCombine(previewHash, hashFloat(panel_.waveDropRadius));
        previewHash = hashCombine(previewHash, static_cast<std::uint64_t>(panel_.waveDropCount));
        previewHash = hashCombine(previewHash, hashFloat(panel_.waveDropJitter));
        previewHash = hashCombine(previewHash, hashFloat(panel_.waveRingFrequency));

        if (previewHash != wizardPreviewHash_ || wizardPreviewW_ != previewW || wizardPreviewH_ != previewH) {
            const std::size_t pixelCount = static_cast<std::size_t>(previewW) * static_cast<std::size_t>(previewH);
            std::vector<float> previewPrimary(pixelCount, 0.0f);
            std::vector<float> previewTerrain(pixelCount, 0.5f);
            std::vector<float> previewWater(pixelCount, 0.0f);
            std::vector<float> previewHumidity(pixelCount, 0.0f);
            std::vector<float> previewWindU(pixelCount, 0.0f);
            std::vector<float> previewWindV(pixelCount, 0.0f);

            const auto noise2D = [&](const std::uint64_t salt, const int x, const int y) {
                std::uint64_t h = panel_.seed ^ salt;
                h = hashCombine(h, static_cast<std::uint64_t>(x * 73856093));
                h = hashCombine(h, static_cast<std::uint64_t>(y * 19349663));
                h ^= (h >> 33u);
                h *= 0xff51afd7ed558ccdull;
                h ^= (h >> 33u);
                h *= 0xc4ceb9fe1a85ec53ull;
                h ^= (h >> 33u);
                return static_cast<float>(h & 0xFFFFFFFFull) / static_cast<float>(0xFFFFFFFFull);
            };

            auto indexOf = [&](const int x, const int y) {
                return static_cast<std::size_t>(y) * static_cast<std::size_t>(previewW) + static_cast<std::size_t>(x);
            };

            const InitialConditionType modeType = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
            if (modeType == InitialConditionType::Terrain) {
                for (int y = 0; y < previewH; ++y) {
                    for (int x = 0; x < previewW; ++x) {
                        const std::size_t idx = indexOf(x, y);
                        const float terrain = previewTerrainValue(panel_, x * previewStride, y * previewStride, previewBaseW, previewBaseH);
                        const float water = std::clamp((panel_.seaLevel - terrain) * 1.9f + 0.10f, 0.0f, 1.0f);
                        const float humidity = std::clamp(
                            panel_.humidityFromWater * water +
                            (0.25f + 0.75f * panel_.biomeNoiseStrength) * noise2D(0xA5ull, x, y),
                            0.0f,
                            1.0f);
                        previewPrimary[idx] = terrain;
                        previewTerrain[idx] = terrain;
                        previewWater[idx] = water;
                        previewHumidity[idx] = humidity;
                    }
                }
            } else if (modeType == InitialConditionType::Conway) {
                std::vector<float> aliveMask(pixelCount, 0.0f);
                for (int y = 0; y < previewH; ++y) {
                    for (int x = 0; x < previewW; ++x) {
                        const std::size_t idx = indexOf(x, y);
                        aliveMask[idx] = noise2D(0xC0ull, x, y) < panel_.conwayAliveProbability ? 1.0f : 0.0f;
                    }
                }

                const int smoothingPasses = std::clamp(panel_.conwaySmoothingPasses, 0, 3);
                for (int pass = 0; pass < smoothingPasses; ++pass) {
                    std::vector<float> nextMask = aliveMask;
                    for (int y = 1; y < previewH - 1; ++y) {
                        for (int x = 1; x < previewW - 1; ++x) {
                            int neighbors = 0;
                            for (int oy = -1; oy <= 1; ++oy) {
                                for (int ox = -1; ox <= 1; ++ox) {
                                    if (ox == 0 && oy == 0) {
                                        continue;
                                    }
                                    neighbors += aliveMask[indexOf(x + ox, y + oy)] > 0.5f ? 1 : 0;
                                }
                            }
                            nextMask[indexOf(x, y)] = neighbors >= 4 ? 1.0f : 0.0f;
                        }
                    }
                    aliveMask.swap(nextMask);
                }

                for (std::size_t i = 0; i < pixelCount; ++i) {
                    const float alive = aliveMask[i];
                    previewPrimary[i] = (alive > 0.5f) ? panel_.conwayAliveValue : panel_.conwayDeadValue;
                    previewTerrain[i] = 0.3f + 0.6f * alive;
                    previewWater[i] = std::clamp(0.35f - 0.25f * alive, 0.0f, 1.0f);
                    previewHumidity[i] = 0.25f + 0.55f * alive;
                }
            } else if (modeType == InitialConditionType::GrayScott) {
                std::vector<float> fieldA(pixelCount, panel_.grayScottBackgroundA);
                std::vector<float> fieldB(pixelCount, panel_.grayScottBackgroundB);
                const int spotCount = std::clamp(panel_.grayScottSpotCount, 1, 64);
                const float baseRadius = std::max(1.0f, panel_.grayScottSpotRadius / static_cast<float>(previewStride));

                for (int spot = 0; spot < spotCount; ++spot) {
                    const float centerX = noise2D(0x901ull + static_cast<std::uint64_t>(spot), spot, previewW) * static_cast<float>(previewW - 1);
                    const float centerY = noise2D(0xA11ull + static_cast<std::uint64_t>(spot), previewH, spot) * static_cast<float>(previewH - 1);
                    const float radius = baseRadius * (1.0f + panel_.grayScottSpotJitter * (noise2D(0xB21ull + static_cast<std::uint64_t>(spot), spot, spot) - 0.5f));
                    const float radiusSafe = std::max(0.5f, radius);

                    for (int y = 0; y < previewH; ++y) {
                        for (int x = 0; x < previewW; ++x) {
                            const float dx = static_cast<float>(x) - centerX;
                            const float dy = static_cast<float>(y) - centerY;
                            const float d2 = dx * dx + dy * dy;
                            const float influence = std::exp(-d2 / (2.0f * radiusSafe * radiusSafe));
                            const std::size_t idx = indexOf(x, y);
                            fieldA[idx] = fieldA[idx] * (1.0f - influence) + panel_.grayScottSpotValueA * influence;
                            fieldB[idx] = fieldB[idx] * (1.0f - influence) + panel_.grayScottSpotValueB * influence;
                        }
                    }
                }

                auto normalizeRange = [&](const std::vector<float>& input) {
                    std::vector<float> out(input.size(), 0.0f);
                    float minV = std::numeric_limits<float>::infinity();
                    float maxV = -std::numeric_limits<float>::infinity();
                    for (const float v : input) {
                        minV = std::min(minV, v);
                        maxV = std::max(maxV, v);
                    }
                    const float span = std::max(1e-6f, maxV - minV);
                    for (std::size_t i = 0; i < input.size(); ++i) {
                        out[i] = std::clamp((input[i] - minV) / span, 0.0f, 1.0f);
                    }
                    return out;
                };

                previewPrimary = fieldB;
                previewTerrain = normalizeRange(fieldA);
                previewWater = normalizeRange(fieldB);
                for (std::size_t i = 0; i < pixelCount; ++i) {
                    previewHumidity[i] = std::clamp(0.5f * previewTerrain[i] + 0.5f * previewWater[i], 0.0f, 1.0f);
                }
            } else if (modeType == InitialConditionType::Waves) {
                std::vector<float> wave(pixelCount, panel_.waveBaseline);
                const int dropCount = std::clamp(panel_.waveDropCount, 1, 24);
                const float baseRadius = std::max(1.0f, panel_.waveDropRadius / static_cast<float>(previewStride));
                for (int d = 0; d < dropCount; ++d) {
                    const float jitterX = (noise2D(0xD11ull + static_cast<std::uint64_t>(d), d, previewW) - 0.5f) * panel_.waveDropJitter;
                    const float jitterY = (noise2D(0xD21ull + static_cast<std::uint64_t>(d), previewH, d) - 0.5f) * panel_.waveDropJitter;
                    const float centerX = static_cast<float>(previewW - 1) * (0.5f + jitterX);
                    const float centerY = static_cast<float>(previewH - 1) * (0.5f + jitterY);
                    const float radius = std::max(0.75f, baseRadius * (0.8f + 0.4f * noise2D(0xD31ull, d, d)));

                    for (int y = 0; y < previewH; ++y) {
                        for (int x = 0; x < previewW; ++x) {
                            const float dx = static_cast<float>(x) - centerX;
                            const float dy = static_cast<float>(y) - centerY;
                            const float dist = std::sqrt(dx * dx + dy * dy);
                            const float normalizedDist = dist / radius;
                            const float envelope = std::exp(-normalizedDist * normalizedDist);
                            const float ring = std::cos(normalizedDist * panel_.waveRingFrequency * 3.1415926f);
                            wave[indexOf(x, y)] += panel_.waveDropAmplitude * envelope * ring;
                        }
                    }
                }

                auto normalizeWave = [&](const std::vector<float>& input) {
                    std::vector<float> out(input.size(), 0.0f);
                    float minV = std::numeric_limits<float>::infinity();
                    float maxV = -std::numeric_limits<float>::infinity();
                    for (const float v : input) {
                        minV = std::min(minV, v);
                        maxV = std::max(maxV, v);
                    }
                    const float span = std::max(1e-6f, maxV - minV);
                    for (std::size_t i = 0; i < input.size(); ++i) {
                        out[i] = std::clamp((input[i] - minV) / span, 0.0f, 1.0f);
                    }
                    return out;
                };

                previewPrimary = wave;
                previewTerrain = normalizeWave(wave);
                for (std::size_t i = 0; i < pixelCount; ++i) {
                    previewWater[i] = std::clamp(std::abs(previewTerrain[i] - 0.5f) * 2.0f, 0.0f, 1.0f);
                    previewHumidity[i] = std::clamp(0.35f + 0.65f * previewWater[i], 0.0f, 1.0f);
                }

                for (int y = 1; y < previewH - 1; ++y) {
                    for (int x = 1; x < previewW - 1; ++x) {
                        const float gx = wave[indexOf(x + 1, y)] - wave[indexOf(x - 1, y)];
                        const float gy = wave[indexOf(x, y + 1)] - wave[indexOf(x, y - 1)];
                        const std::size_t idx = indexOf(x, y);
                        previewWindU[idx] = gx;
                        previewWindV[idx] = gy;
                    }
                }
            } else {
                for (std::size_t i = 0; i < pixelCount; ++i) {
                    previewPrimary[i] = 0.0f;
                    previewTerrain[i] = 0.5f;
                    previewWater[i] = 0.0f;
                    previewHumidity[i] = 0.0f;
                }
            }

            std::vector<float> displayPrimary = previewPrimary;
            switch (sessionUi_.generationPreviewSourceIndex) {
                case 1: displayPrimary = previewPrimary; break;
                case 2: displayPrimary = previewTerrain; break;
                case 3: displayPrimary = previewWater; break;
                case 4: displayPrimary = previewHumidity; break;
                case 5: {
                    displayPrimary.assign(pixelCount, 0.0f);
                    for (std::size_t i = 0; i < pixelCount; ++i) {
                        const float m = std::sqrt(previewWindU[i] * previewWindU[i] + previewWindV[i] * previewWindV[i]);
                        displayPrimary[i] = m;
                    }
                    break;
                }
                case 6: {
                    if (!sessionUi_.selectedModelCellStateVariables.empty()) {
                        const int channelIndex = std::clamp(
                            sessionUi_.generationPreviewChannelIndex,
                            0,
                            static_cast<int>(sessionUi_.selectedModelCellStateVariables.size()) - 1);
                        const std::string& channel = sessionUi_.selectedModelCellStateVariables[static_cast<std::size_t>(channelIndex)];
                        const float blend = 0.12f + 0.08f * std::clamp(static_cast<float>(channel.size()) / 24.0f, 0.0f, 1.0f);
                        displayPrimary.resize(pixelCount, 0.0f);
                        for (int y = 0; y < previewH; ++y) {
                            for (int x = 0; x < previewW; ++x) {
                                const std::size_t idx = indexOf(x, y);
                                const float channelNoise = noise2D(0xEE11ull + static_cast<std::uint64_t>(channelIndex), x, y);
                                displayPrimary[idx] = (1.0f - blend) * previewPrimary[idx] + blend * channelNoise;
                            }
                        }
                    }
                    break;
                }
                default: {
                    if (modeType == InitialConditionType::Terrain) displayPrimary = previewTerrain;
                    else if (modeType == InitialConditionType::GrayScott) displayPrimary = previewWater;
                    else if (modeType == InitialConditionType::Waves) displayPrimary = previewPrimary;
                    else if (modeType == InitialConditionType::Conway) displayPrimary = previewPrimary;
                    else displayPrimary = previewPrimary;
                    break;
                }
            }

            DisplayBuffer previewDisplay = buildDisplayBufferFromPreviewComponents(
                displayPrimary,
                previewTerrain,
                previewWater,
                previewHumidity,
                previewWindU,
                previewWindV,
                viz_.generationPreviewDisplayType,
                previewViewportConfig.displayManager,
                "preview");

            wizardPreviewPixels_.assign(pixelCount * 4u, 0u);
            for (std::size_t idx = 0; idx < pixelCount; ++idx) {
                const float value = idx < previewDisplay.values.size() ? previewDisplay.values[idx] : 0.0f;
                const float normalized = std::clamp((value - previewDisplay.minValue) / std::max(0.0001f, previewDisplay.maxValue - previewDisplay.minValue), 0.0f, 1.0f);
                const ImU32 color = mapDisplayTypeColor(
                    (viz_.generationPreviewDisplayType == DisplayType::ScalarField ||
                     viz_.generationPreviewDisplayType == DisplayType::WaterDepth ||
                     viz_.generationPreviewDisplayType == DisplayType::WindField) ? normalized : value,
                    viz_.generationPreviewDisplayType,
                    ColorMapMode::Turbo,
                    previewViewportConfig);
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
        const std::string sourceLabel = std::string("Source: ") + kPreviewSources[static_cast<std::size_t>(std::clamp(sessionUi_.generationPreviewSourceIndex, 0, 6))];
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 18.0f), IM_COL32(188, 200, 226, 255), sourceLabel.c_str());
        if (sessionUi_.generationPreviewSourceIndex == 6 && !sessionUi_.selectedModelCellStateVariables.empty()) {
            const int channelIndex = std::clamp(
                sessionUi_.generationPreviewChannelIndex,
                0,
                static_cast<int>(sessionUi_.selectedModelCellStateVariables.size()) - 1);
            const std::string channelLabel = "x_i: " + sessionUi_.selectedModelCellStateVariables[static_cast<std::size_t>(channelIndex)];
            dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 54.0f), IM_COL32(188, 200, 226, 255), channelLabel.c_str());
        }
        const std::string autoLevel = "Water level: " + std::to_string(wizardPreviewWaterLevel_).substr(0, 5) + (previewViewportConfig.displayManager.autoWaterLevel ? " (auto)" : " (manual)");
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 36.0f), IM_COL32(188, 200, 226, 255), autoLevel.c_str());
        if (wizardPreviewStride_ > 1) {
            const std::string quality = "Preview stride: 1/" + std::to_string(wizardPreviewStride_) + " for performance";
            dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 54.0f), IM_COL32(188, 200, 226, 255), quality.c_str());
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

        const bool wizardBusy = deferredWizardInitialization_.active;
        const bool hasBlockingPreflight = !verification.blocking.empty();
        const bool hasBindingBlocking = sessionUi_.generationBindingPlan.hasBlockingIssues() && !sessionUi_.allowUnresolvedGenerationBindings;
        const bool canAdvance =
            (sessionUi_.wizardStepIndex == 0) ||
            (sessionUi_.wizardStepIndex == 1 && !hasBindingBlocking) ||
            (sessionUi_.wizardStepIndex == 2 && !hasBlockingPreflight);
        const bool atFinalStep = (sessionUi_.wizardStepIndex == 3);

        if (wizardBusy) {
            ImGui::TextColored(ImVec4(0.62f, 0.82f, 0.95f, 1.0f), "Creating world...");
            ImGui::ProgressBar(
                std::clamp(sessionUi_.operationProgress, 0.0f, 1.0f),
                ImVec2(-1.0f, 0.0f),
                sessionUi_.operationDetail[0] != '\0' ? sessionUi_.operationDetail : "Applying initialization");
            ImGui::BeginDisabled();
        }

        if (PrimaryButton(atFinalStep ? "Create world" : "Next step", ImVec2(btnW, 44.0f))) {
            if (!atFinalStep) {
                if (canAdvance) {
                    sessionUi_.wizardStepIndex = std::min(3, sessionUi_.wizardStepIndex + 1);
                } else {
                    if (sessionUi_.wizardStepIndex == 1 && hasBindingBlocking) {
                        setSessionStatusFromRaw("wizard_step_blocked reason=unresolved_bindings");
                    } else if (sessionUi_.wizardStepIndex == 2 && hasBlockingPreflight) {
                        setSessionStatusFromRaw("wizard_step_blocked reason=preflight_blocking");
                    }
                }
            } else {
            if (!panel_.useManualSeed) {
                panel_.seed = generateRandomSeed();
            }

            VerificationReport verification = collectVerificationReport();

            bool preflightBlocked = false;
            std::string preflightReason;
            const InitialConditionType selectedType = static_cast<InitialConditionType>(
                std::clamp(panel_.initialConditionTypeIndex, 0, static_cast<int>(InitialConditionType::DiffusionLimit)));

            if (!verification.blocking.empty()) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=verification_failed";
                for (const auto& msg : verification.blocking) {
                    appendLog("verification_blocking: " + msg);
                }
            }

            if (sessionUi_.selectedModelCatalog.variables.empty()) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=model_catalog_unavailable";
            }

            if (!preflightBlocked && selectedType == InitialConditionType::Conway && std::strlen(panel_.conwayTargetVariable) == 0u) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=missing_conway_target_variable";
            }
            if (!preflightBlocked && selectedType == InitialConditionType::GrayScott &&
                (std::strlen(panel_.grayScottTargetVariableA) == 0u || std::strlen(panel_.grayScottTargetVariableB) == 0u)) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=missing_gray_scott_target_variable";
            }
            if (!preflightBlocked && selectedType == InitialConditionType::Waves && std::strlen(panel_.wavesTargetVariable) == 0u) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=missing_waves_target_variable";
            }
            if (!preflightBlocked && !isRuntimeSupportedGenerationMode(selectedType)) {
                preflightBlocked = true;
                preflightReason = "world_create_blocked reason=unsupported_generation_mode";
            }

            if (preflightBlocked) {
                setSessionStatusFromRaw(preflightReason);
                appendLog(preflightReason);
            }

            if (!preflightBlocked) {
                initialization::InitializationRequest verifyRequest;
                verifyRequest.type = selectedType;
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
                    setSessionStatusFromRaw(status.str());
                    appendLog(status.str());
                    preflightBlocked = true;
                }
            }

            if (!preflightBlocked) {
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

                beginOperationStatus("world creation", 0.25f, worldName.c_str());
                const auto createStartedAt = std::chrono::steady_clock::now();
                std::string message;
                if (runtime_.createWorld(worldName, runtime_.config(), message)) {
                    appendLog(message);
                    const InitialConditionType modeType = static_cast<InitialConditionType>(panel_.initialConditionTypeIndex);
                    const std::string conwayTarget = panel_.conwayTargetVariable;
                    const std::string grayTargetA = panel_.grayScottTargetVariableA;
                    const std::string grayTargetB = panel_.grayScottTargetVariableB;
                    const std::string wavesTarget = panel_.wavesTargetVariable;

                    deferredWizardInitialization_ = DeferredWizardInitialization{};
                    deferredWizardInitialization_.active = true;
                    deferredWizardInitialization_.startedAt = std::chrono::steady_clock::now();
                    deferredWizardInitialization_.verificationWarnings = verification.warnings;

                    for (const auto& setting : sessionUi_.variableInitializationSettings) {
                        if (!setting.enabled || setting.variableId.empty()) {
                            continue;
                        }

                        const bool modeOwnsTarget =
                            (modeType == InitialConditionType::Conway && setting.variableId == conwayTarget) ||
                            (modeType == InitialConditionType::GrayScott && (setting.variableId == grayTargetA || setting.variableId == grayTargetB)) ||
                            (modeType == InitialConditionType::Waves && setting.variableId == wavesTarget);
                        if (modeOwnsTarget) {
                            continue;
                        }

                        deferredWizardInitialization_.pendingSettings.push_back(setting);
                    }

                    const std::uint64_t cellCount =
                        static_cast<std::uint64_t>(std::max(1, panel_.gridWidth)) *
                        static_cast<std::uint64_t>(std::max(1, panel_.gridHeight));
                    deferredWizardInitialization_.estimatedWrites =
                        cellCount * static_cast<std::uint64_t>(deferredWizardInitialization_.pendingSettings.size());

                    completeOperationStatus(createStartedAt, "world created, applying variable initializers");
                    if (deferredWizardInitialization_.pendingSettings.empty()) {
                        tickDeferredWizardInitialization();
                    } else {
                        const std::string detail =
                            "Applying variable initializers 0/" +
                            std::to_string(deferredWizardInitialization_.pendingSettings.size());
                        beginOperationStatus("world creation", 0.0f, detail.c_str());
                    }
                } else {
                    appendLog(message);
                    completeOperationStatus(createStartedAt, "world creation failed");
                    setSessionStatusFromRaw(message);
                }
            }
            }
        }
        DelayedTooltip(atFinalStep
            ? "Applies generation settings, creates the world, and enters simulation view."
            : "Continue to the next wizard step.");

        ImGui::SameLine();
        if (SecondaryButton(sessionUi_.wizardStepIndex == 0 ? "Back to world selection" : "Previous step", ImVec2(btnW, 44.0f))) {
            if (sessionUi_.wizardStepIndex == 0) {
                appState_ = AppState::SessionManager;
            } else {
                sessionUi_.wizardStepIndex = std::max(0, sessionUi_.wizardStepIndex - 1);
            }
        }
        DelayedTooltip(sessionUi_.wizardStepIndex == 0
            ? "Return to world selection without creating a world."
            : "Return to the previous wizard step.");

        ImGui::SameLine();
        if (SecondaryButton("Reset parameters", ImVec2(btnW, 44.0f))) {
            showWizardResetConfirm_ = true;
            ImGui::OpenPopup("Reset Wizard Parameters");
        }
        DelayedTooltip("Restores parameters from the current runtime configuration.");

        if (wizardBusy) {
            ImGui::EndDisabled();
        }

        if (sessionUi_.statusMessage[0] != '\0') {
            LabeledHint(sessionUi_.statusMessage);
            if (sessionUi_.statusTechnicalDetail[0] != '\0') {
                ImGui::TextDisabled("Technical detail available. Hover for raw reason code.");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", sessionUi_.statusTechnicalDetail);
                }
            }
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
            ensureViewportStateConsistency();
            dockLayoutInitialized_ = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.65f, nullptr, &dockMain);
            ImGuiID dockBottomLeft = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.5f, nullptr, &dockLeft);
            ImGuiID dockTopRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.55f, nullptr, &dockMain);

            for (std::size_t i = 0; i < viz_.viewports.size(); ++i) {
                const std::string viewWindowName = runtimeViewportWindowName(i);
                ImGuiID targetDock = dockMain;
                if (i == 0u) {
                    targetDock = dockLeft;
                } else if (i == 1u) {
                    targetDock = dockBottomLeft;
                } else if (i == 2u) {
                    targetDock = dockTopRight;
                }
                ImGui::DockBuilderDockWindow(viewWindowName.c_str(), targetDock);
            }
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
