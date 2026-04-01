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
        SectionHeader("World Simulator", "Select a world or create a new one.");
        ImGui::SameLine(std::max(0.0f, ImGui::GetContentRegionAvail().x - 132.0f));
        if (SecondaryButton("Refresh list", ImVec2(128.0f, 32.0f))) {
            sessionUi_.needsRefresh = true;
        }
        DelayedTooltip("Refreshes the world list from stored profiles and checkpoints.");
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float actionBarH = 92.0f;
        const float contentH = std::max(220.0f, ImGui::GetContentRegionAvail().y - actionBarH - kS3);
        const bool narrowLayout = rootW < 1100.0f;

        ImGui::BeginChild("SessionContent", ImVec2(-1.0f, contentH), false);
        if (!narrowLayout) {
            ImGui::Columns(2, "session_cols", false);
            ImGui::SetColumnWidth(0, rootW * 0.38f);
        }

        SectionHeader("Saved worlds", "Choose a world to inspect or open.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldList", ImVec2(-1.0f, narrowLayout ? 210.0f : -1.0f), true);
        if (sessionUi_.worlds.empty()) {
            EmptyStateCard("No saved worlds found.", "Create a new world to generate your first simulation.");
        } else {
            for (int i = 0; i < static_cast<int>(sessionUi_.worlds.size()); ++i) {
                const auto& world = sessionUi_.worlds[static_cast<std::size_t>(i)];
                const bool selected = (sessionUi_.selectedWorldIndex == i);
                if (ImGui::Selectable(world.worldName.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 30.0f))) {
                    sessionUi_.selectedWorldIndex = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openSelectedWorld();
                    }
                }
            }
        }
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("World details", "Review world metadata before opening.");
        ImGui::Spacing();
        ImGui::BeginChild("WorldDetails", ImVec2(-1.0f, -1.0f), true);
        if (sessionUi_.selectedWorldIndex >= 0 && sessionUi_.selectedWorldIndex < static_cast<int>(sessionUi_.worlds.size())) {
            const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
            ImGui::Text("Name: %s", world.worldName.c_str());
            ImGui::Text("Grid: %ux%u", world.gridWidth, world.gridHeight);
            ImGui::Text("Seed: %llu", static_cast<unsigned long long>(world.seed));
            ImGui::Text("Tier: %s", world.tier.empty() ? "n/a" : world.tier.c_str());
            ImGui::Text("Temporal: %s", world.temporalPolicy.empty() ? "n/a" : world.temporalPolicy.c_str());
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
        const float btnW = std::max(160.0f, (w - (2.0f * kS2)) / 3.0f);

        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (PrimaryButton("Open world", ImVec2(btnW, 42.0f))) {
            openSelectedWorld();
        }
        // Debug: log button state
        static bool lastFrameHovered = false;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!lastFrameHovered) {
                lastFrameHovered = true;
            }
        } else {
            lastFrameHovered = false;
        }
        DelayedTooltip("Loads the selected world without restarting the application.");
        if (!canOpen) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (SecondaryButton("Create world", ImVec2(btnW, 42.0f))) {
            const std::string suggestedName = runtime_.suggestNextWorldName();
            std::snprintf(sessionUi_.pendingWorldName, sizeof(sessionUi_.pendingWorldName), "%s", suggestedName.c_str());
            syncPanelFromConfig();
            panel_.useManualSeed = false;
            panel_.seed = generateRandomSeed();
            appState_ = AppState::NewWorldWizard;
        }
        DelayedTooltip("Opens the creation wizard to define generation parameters.");

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
        LabeledHint("World names are auto-suggested and must be unique in storage.");
        ImGui::Spacing();
        drawGridSetupSection();
        ImGui::Spacing();
        drawTierSelector();
        ImGui::Spacing();
        drawWorldGenerationSection();
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
            applyConfigFromPanel();
            std::string worldName = sessionUi_.pendingWorldName;
            if (worldName.empty()) {
                worldName = runtime_.suggestNextWorldName();
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
        if (SecondaryButton("Back to session manager", ImVec2(btnW, 44.0f))) {
            appState_ = AppState::SessionManager;
            sessionUi_.needsRefresh = true;
        }
        DelayedTooltip("Returns to the world selection page without creating a new world.");

        ImGui::SameLine();
        if (SecondaryButton("Reset parameters", ImVec2(btnW, 44.0f))) {
            syncPanelFromConfig();
            panel_.useManualSeed = false;
            panel_.seed = generateRandomSeed();
            std::string suggestedName = runtime_.suggestNextWorldName();
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
