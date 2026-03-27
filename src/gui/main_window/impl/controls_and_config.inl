#ifdef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    void drawControlPanel() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, 425.0f), ImGuiCond_FirstUseEver);

        ImGui::Begin("Control Panel");

        drawStatusHeader();

        if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Session")) {
                drawSimulationDetailsSection();
                drawSessionLifecycleSection();
                drawPerformanceSection();
                drawProfilesAndLogSection();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Display")) {
                drawDisplayMappingSection();
                drawOverlaysSection();
                drawOpticsSection();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Environment")) {
                drawPhysicsSection();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Diagnostics")) {
                drawAnalysisSection();
                drawAccessibilitySection();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void drawStatusHeader() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(15, 18, 26, 255));
        ImGui::BeginChild("StatusHeader", ImVec2(0, 60), true);
        
        ImGui::Columns(3, nullptr, false);
        ImGui::SetColumnWidth(0, 150.0f);

        if (SecondaryButton("Return to session manager", ImVec2(-1.0f, 32.0f))) {
            saveDisplayPrefs();
            std::string saveMessage;
            if (!runtime_.activeWorldName().empty()) {
                if (runtime_.saveActiveWorld(saveMessage)) {
                    appendLog(saveMessage);
                } else {
                    appendLog(saveMessage);
                }
            }
            std::string msg;
            runtime_.stop(msg);
            appendLog(msg);
            viz_.autoRun = false;
            appState_ = AppState::SessionManager;
            sessionUi_.needsRefresh = true;
        }
        DelayedTooltip("Saves the active world and returns to world selection.");

        ImGui::NextColumn();
        ImGui::SetColumnWidth(1, 200.0f);

        StatusBadge(runtime_.isRunning() ? "RUNNING" : "STOPPED", runtime_.isRunning());
        ImGui::SameLine();
        StatusBadge(runtime_.isPaused() ? "PAUSED" : "ACTIVE", !runtime_.isPaused());

        if (viz_.hasCachedCheckpoint) {
            ImGui::Text("Step: %llu", static_cast<unsigned long long>(viz_.cachedCheckpoint.stateSnapshot.header.stepIndex));
        }

        ImGui::NextColumn();
        if (viz_.hasCachedCheckpoint) {
            ImGui::Text("Grid: %ux%u", viz_.cachedCheckpoint.stateSnapshot.grid.width, viz_.cachedCheckpoint.stateSnapshot.grid.height);
        }
        if (viz_.lastRuntimeError[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Error detected!");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Engine Health: Nominal");
        }

        ImGui::Columns(1);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    void drawSimulationDetailsSection() {
        PushSectionTint(0);
        if (ImGui::CollapsingHeader("Active Simulation Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (viz_.hasCachedCheckpoint) {
                const auto& sig = viz_.cachedCheckpoint.runSignature;
                ImGui::Text("Identity:"); ImGui::SameLine(100); ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%016llx", static_cast<unsigned long long>(sig.identityHash()));
                ImGui::Text("Steps:"); ImGui::SameLine(100); ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "%llu", static_cast<unsigned long long>(viz_.cachedCheckpoint.stateSnapshot.header.stepIndex));
                ImGui::Text("Grid:"); ImGui::SameLine(100); ImGui::Text("%ux%u (Total %u cells)", sig.grid().width, sig.grid().height, sig.grid().width * sig.grid().height);
                ImGui::Text("Policy:"); ImGui::SameLine(100); ImGui::Text("%s", app::temporalPolicyToString(sig.temporalPolicy()).c_str());
                ImGui::Text("State Size:"); ImGui::SameLine(100); ImGui::Text("%.2f MB", static_cast<float>(viz_.cachedCheckpoint.stateSnapshot.payloadBytes) / 1048576.0f);
            } else {
                ImGui::TextDisabled("No active simulation data.");
            }
        }
        PopSectionTint();
    }

    void drawSessionLifecycleSection() {
        PushSectionTint(0);
        if (ImGui::CollapsingHeader("Engine Lifecycle & Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool isRunning = runtime_.isRunning();
            bool isPaused = runtime_.isPaused();
            bool isPlaying = isRunning && !isPaused && viz_.autoRun;

            ImVec2 halfBtn = ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f, 36.0f);

            if (!isRunning) {
                if (PrimaryButton("Start Simulation", halfBtn)) {
                    std::string message;
                    runtime_.start(message);
                    viz_.autoRun = true;
                    appendLog(message);
                    refreshFieldNames();
                    requestSnapshotRefresh();
                    triggerOverlay(OverlayIcon::Play);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled();
                PrimaryButton("Stop", ImVec2(-1.0f, 36.0f));
                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::TextDisabled("Simulation is stopped. Press Start to begin.");
            } else {
                if (isPlaying) {
                    if (PrimaryButton("Pause", halfBtn)) {
                        std::string message;
                        runtime_.pause(message);
                        viz_.autoRun = false;
                        appendLog(message);
                        triggerOverlay(OverlayIcon::Pause);
                    }
                } else {
                    if (PrimaryButton("Play / Resume", halfBtn)) {
                        std::string message;
                        runtime_.resume(message);
                        viz_.autoRun = true;
                        appendLog(message);
                        requestSnapshotRefresh();
                        triggerOverlay(OverlayIcon::Play);
                    }
                }
                ImGui::SameLine();
                if (PrimaryButton("Stop & Reset", ImVec2(-1.0f, 36.0f))) {
                    std::string message;
                    viz_.autoRun = false;
                    runtime_.stop(message);
                    appendLog(message);
                    requestSnapshotRefresh();
                    triggerOverlay(OverlayIcon::Pause);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (isPlaying) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mode: Continuous Auto-Run");
                    sliderIntWithHint("Simulation Tick Rate (Hz)", &viz_.simulationTickHz, 1, 480, "Target simulation stepping rate while auto-run is active.");
                    sliderIntWithHint("Steps per Tick", &viz_.autoStepsPerFrame, 1, 512, "How many simulation steps are executed each tick.");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Mode: Manual Stepping / Paused");
                    
                    sliderIntWithHint("Step Size", &panel_.stepCount, 1, 1000000, "Number of steps to advance when pressing Step Forward.");
                    if (PrimaryButton("Advance Step(s)", ImVec2(-1.0f, 28))) {
                        std::string message;
                        runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), message);
                        appendLog(message);
                        requestSnapshotRefresh();
                    }

                    ImGui::Spacing();
                    checkboxWithHint("Show advanced stepping", &panel_.showAdvancedStepping, "Shows target-step fast-forward controls for long simulation jumps.");
                    if (panel_.showAdvancedStepping) {
                        int runUntil = static_cast<int>(std::min<std::uint64_t>(panel_.runUntilTarget, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
                        if (sliderIntWithHint("Target Step Index", &runUntil, 0, kImGuiIntSafeMax, "Advance simulation until this absolute step index is reached.")) {
                            panel_.runUntilTarget = static_cast<std::uint64_t>(runUntil);
                        }
                        if (PrimaryButton("Fast-Forward to Target", ImVec2(-1.0f, 28))) {
                            std::string message;
                            runtime_.runUntil(panel_.runUntilTarget, message);
                            appendLog(message);
                            requestSnapshotRefresh();
                        }
                    }
                }
            }
        }
        PopSectionTint();
    }

    void drawPerformanceSection() {
        PushSectionTint(1);
        if (ImGui::CollapsingHeader("Performance & Sync", 0)) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Snapshot mapping time: %.2f ms", viz_.lastSnapshotDurationMs);
            
            sliderFloatWithHint("Viewport Refresh Rate (Hz)", &viz_.snapshotRefreshHz, 1.0f, 120.0f, "%.1f", "Target rate used to refresh rendered snapshots in the interface.");
            ImGui::TextDisabled("Simulation tick rate and viewport refresh rate are independent on purpose.");

            checkboxWithHint("Adaptive Render Sampling", &viz_.adaptiveSampling, "Automatically increases sampling stride when zoomed out to keep rendering responsive.");
            if (!viz_.adaptiveSampling) {
                sliderIntWithHint("Manual stride", &viz_.manualSamplingStride, 1, 64, "Render one cell every N cells when adaptive sampling is disabled.");
            }
            sliderIntWithHint("Max rendered cells", &viz_.maxRenderedCells, 1000, 2000000, "Hard cap on drawn cells per frame across panels.");

            if (PrimaryButton("Force Snapshot Refresh", ImVec2(-1.0f, 24.0f))) {
                requestSnapshotRefresh();
            }
        }
        PopSectionTint();
    }

    void drawProfilesAndLogSection() {
        PushSectionTint(2);
        if (ImGui::CollapsingHeader("Profiles & Events", 0)) {
            inputTextWithHint("Profile", panel_.profileName, sizeof(panel_.profileName), "Profile name used for save/load operations.");
            if (PrimaryButton("Save", ImVec2(70, 26))) {
                std::string message;
                runtime_.saveProfile(panel_.profileName, message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Load", ImVec2(70, 26))) {
                std::string message;
                runtime_.loadProfile(panel_.profileName, message);
                appendLog(message);
                syncPanelFromConfig();
                refreshFieldNames();
                requestSnapshotRefresh();
            }
            ImGui::SameLine();
            if (PrimaryButton("List", ImVec2(70, 26))) {
                std::string message;
                runtime_.listProfiles(message);
                appendLog(message);
            }

            ImGui::Separator();
            ImGui::Text("Event Log:");
            ImGui::BeginChild("log_scroller", ImVec2(0, 180), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if (!logs_.empty()) {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(logs_.size()));
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        ImGui::TextUnformatted(logs_[static_cast<std::size_t>(i)].c_str());
                    }
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        PopSectionTint();
    }

    [[nodiscard]] std::filesystem::path displayPrefsPathForWorld(const std::string& worldName) const {
        if (worldName.empty()) {
            return {};
        }
        return std::filesystem::path("checkpoints") / "worlds" / (worldName + ".displayprefs");
    }

    [[nodiscard]] std::filesystem::path activeDisplayPrefsPath() const {
        return displayPrefsPathForWorld(runtime_.activeWorldName());
    }

    void openSelectedWorld() {
        if (sessionUi_.selectedWorldIndex < 0 || sessionUi_.selectedWorldIndex >= static_cast<int>(sessionUi_.worlds.size())) {
            return;
        }

        const auto& world = sessionUi_.worlds[static_cast<std::size_t>(sessionUi_.selectedWorldIndex)];
        std::string message;
        if (runtime_.openWorld(world.worldName, message)) {
            appendLog(message);
            refreshFieldNames();
            resetDisplayConfigToDefaults();
            loadDisplayPrefs();
            enterSimulationPaused();
        } else {
            appendLog(message);
            std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "%s", message.c_str());
        }
    }

    void resetDisplayConfigToDefaults() {
        const VisualizationState defaults{};
        viz_.layout = defaults.layout;
        viz_.viewports = defaults.viewports;
        viz_.activeViewportEditor = defaults.activeViewportEditor;
        viz_.displayManager = defaults.displayManager;
        viz_.generationPreviewDisplayType = defaults.generationPreviewDisplayType;
    }

    void saveDisplayPrefs() {
        const auto path = activeDisplayPrefsPath();
        if (path.empty()) {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return;
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return;
        out << "layout=" << static_cast<int>(viz_.layout) << "\n";
        out << "generationPreviewDisplayType=" << static_cast<int>(viz_.generationPreviewDisplayType) << "\n";
        out << "displayAutoWaterLevel=" << static_cast<int>(viz_.displayManager.autoWaterLevel) << "\n";
        out << "displayWaterThreshold=" << viz_.displayManager.waterLevel << "\n";
        out << "displayWaterQuantile=" << viz_.displayManager.autoWaterQuantile << "\n";
        out << "displayLowlandThreshold=" << viz_.displayManager.lowlandThreshold << "\n";
        out << "displayHighlandThreshold=" << viz_.displayManager.highlandThreshold << "\n";
        out << "displayWaterPresenceThreshold=" << viz_.displayManager.waterPresenceThreshold << "\n";
        for (int i = 0; i < 4; ++i) {
            auto& vp = viz_.viewports[i];
            out << "vp" << i << "_primaryFieldIndex=" << vp.primaryFieldIndex << "\n";
            out << "vp" << i << "_displayType=" << static_cast<int>(vp.displayType) << "\n";
            out << "vp" << i << "_normalizationMode=" << static_cast<int>(vp.normalizationMode) << "\n";
            out << "vp" << i << "_colorMapMode=" << static_cast<int>(vp.colorMapMode) << "\n";
            out << "vp" << i << "_showVectorField=" << vp.showVectorField << "\n";
            out << "vp" << i << "_vectorXFieldIndex=" << vp.vectorXFieldIndex << "\n";
            out << "vp" << i << "_vectorYFieldIndex=" << vp.vectorYFieldIndex << "\n";
            out << "vp" << i << "_showLegend=" << vp.showLegend << "\n";
        }
    }

    void loadDisplayPrefs() {
        const auto path = activeDisplayPrefsPath();
        if (path.empty()) {
            return;
        }

        std::ifstream in(path);
        if (!in.is_open()) return;
        std::string line;
        while (std::getline(in, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            try {
                if (key == "displayWaterThreshold") {
                    viz_.displayManager.waterLevel = std::stof(line.substr(eq + 1));
                    continue;
                }
                if (key == "displayWaterQuantile") {
                    viz_.displayManager.autoWaterQuantile = std::stof(line.substr(eq + 1));
                    continue;
                }
                if (key == "displayLowlandThreshold") {
                    viz_.displayManager.lowlandThreshold = std::stof(line.substr(eq + 1));
                    continue;
                }
                if (key == "displayHighlandThreshold") {
                    viz_.displayManager.highlandThreshold = std::stof(line.substr(eq + 1));
                    continue;
                }
                if (key == "displayWaterPresenceThreshold") {
                    viz_.displayManager.waterPresenceThreshold = std::stof(line.substr(eq + 1));
                    continue;
                }

                int val = std::stoi(line.substr(eq + 1));
                if (key == "layout") viz_.layout = static_cast<ScreenLayout>(val);
                else if (key == "generationPreviewDisplayType") viz_.generationPreviewDisplayType = static_cast<DisplayType>(val);
                else if (key == "displayAutoWaterLevel") viz_.displayManager.autoWaterLevel = (val != 0);
                else if (key.starts_with("vp")) {
                    int vpIdx = key[2] - '0';
                    if (vpIdx < 0 || vpIdx > 3) continue;
                    auto& vp = viz_.viewports[vpIdx];
                    std::string sub = key.substr(4);
                    if (sub == "primaryFieldIndex") vp.primaryFieldIndex = val;
                    else if (sub == "displayType") vp.displayType = static_cast<DisplayType>(val);
                    else if (sub == "normalizationMode") vp.normalizationMode = static_cast<NormalizationMode>(val);
                    else if (sub == "colorMapMode") vp.colorMapMode = static_cast<ColorMapMode>(val);
                    else if (sub == "showVectorField") vp.showVectorField = (val != 0);
                    else if (sub == "vectorXFieldIndex") vp.vectorXFieldIndex = val;
                    else if (sub == "vectorYFieldIndex") vp.vectorYFieldIndex = val;
                    else if (sub == "showLegend") vp.showLegend = (val != 0);
                }
            } catch(...) {}
        }

        viz_.displayManager.waterLevel = std::clamp(viz_.displayManager.waterLevel, 0.0f, 1.0f);
        viz_.displayManager.autoWaterQuantile = std::clamp(viz_.displayManager.autoWaterQuantile, 0.0f, 1.0f);
        viz_.displayManager.lowlandThreshold = std::clamp(viz_.displayManager.lowlandThreshold, 0.0f, 1.0f);
        viz_.displayManager.highlandThreshold = std::clamp(viz_.displayManager.highlandThreshold, viz_.displayManager.lowlandThreshold + 0.01f, 1.0f);
        viz_.displayManager.waterPresenceThreshold = std::clamp(viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f);
    }

    void drawDisplayMappingSection() {
        PushSectionTint(3);
        if (ImGui::CollapsingHeader("Continuum Field Mapping & Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
            static constexpr std::array<const char*, 4> layoutNames = {
                "Single View",
                "Split Left/Right (Double)",
                "Split Top/Bottom (Double)",
                "4-Way Quad View"
            };
            int layout = static_cast<int>(viz_.layout);
            if (ImGui::Combo("Global View Layout", &layout, layoutNames.data(), static_cast<int>(layoutNames.size()))) {
                viz_.layout = static_cast<ScreenLayout>(std::clamp(layout, 0, static_cast<int>(layoutNames.size()) - 1));
            }
            settingHint("Changes the central grid view to display 1, 2, or 4 viewports.");

            if (PrimaryButton("Refresh Field List", ImVec2(160.0f, 24.0f))) {
                refreshFieldNames();
            }
            ImGui::SameLine();
            if (PrimaryButton("Save Layout", ImVec2(100.0f, 24.0f))) {
                saveDisplayPrefs();
            }
            ImGui::SameLine();
            if (ImGui::Button("Restore Defaults", ImVec2(100.0f, 24.0f))) {
                ImGui::OpenPopup("Revert Settings");
            }

            if (ImGui::BeginPopupModal("Revert Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Are you sure you want to revert to the default double-sided view settings?");
                ImGui::Separator();
                if (ImGui::Button("Yes, Revert", ImVec2(120, 0))) {
                    viz_.layout = ScreenLayout::SplitLeftRight;
                    for (size_t i = 0; i < viz_.viewports.size(); ++i) {
                        auto& vp = viz_.viewports[i];
                        vp.primaryFieldIndex = static_cast<int>(i);
                        vp.displayType = DisplayType::ScalarField;
                        vp.normalizationMode = NormalizationMode::StickyPerField;
                        vp.colorMapMode = (i == 1) ? ColorMapMode::Water : ColorMapMode::Turbo;
                        vp.showVectorField = false;
                        vp.showLegend = true;
                    }
                    saveDisplayPrefs();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Display Type Manager");
            static constexpr std::array<const char*, 4> displayTypeNames = {
                "Scalar Field",
                "Surface Category (water/land/highland)",
                "Relative Elevation",
                "Surface Water (dry=black)"
            };

            int previewMode = static_cast<int>(viz_.generationPreviewDisplayType);
            if (ImGui::Combo("Generation Preview Type", &previewMode, displayTypeNames.data(), static_cast<int>(displayTypeNames.size()))) {
                viz_.generationPreviewDisplayType = static_cast<DisplayType>(std::clamp(previewMode, 0, static_cast<int>(displayTypeNames.size()) - 1));
            }
            settingHint("Display interpretation used by world generation preview.");

            checkboxWithHint("Auto Water Level", &viz_.displayManager.autoWaterLevel, "Automatically computes water level from elevation distribution.");
            if (viz_.displayManager.autoWaterLevel) {
                sliderFloatWithHint("Auto Water Quantile", &viz_.displayManager.autoWaterQuantile, 0.10f, 0.90f, "%.3f", "Target quantile used to infer water level from terrain.");
            } else {
                sliderFloatWithHint("Manual Water Level", &viz_.displayManager.waterLevel, 0.0f, 1.0f, "%.3f", "Manual threshold used to classify submerged areas.");
            }
            sliderFloatWithHint("Lowland Breakpoint", &viz_.displayManager.lowlandThreshold, 0.0f, 1.0f, "%.3f", "Elevation threshold between lowland and upland classes.");
            sliderFloatWithHint("Highland Breakpoint", &viz_.displayManager.highlandThreshold, 0.0f, 1.0f, "%.3f", "Elevation threshold above which terrain is classified as highland.");
            sliderFloatWithHint("Water Presence Threshold", &viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f, "%.3f", "Minimum water signal required to classify a cell as water.");
            viz_.displayManager.waterLevel = std::clamp(viz_.displayManager.waterLevel, 0.0f, 1.0f);
            viz_.displayManager.autoWaterQuantile = std::clamp(viz_.displayManager.autoWaterQuantile, 0.0f, 1.0f);
            viz_.displayManager.lowlandThreshold = std::clamp(viz_.displayManager.lowlandThreshold, 0.0f, 1.0f);
            viz_.displayManager.highlandThreshold = std::clamp(viz_.displayManager.highlandThreshold, viz_.displayManager.lowlandThreshold + 0.01f, 1.0f);
            viz_.displayManager.waterPresenceThreshold = std::clamp(viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f);

            if (PrimaryButton("Save Display Manager", ImVec2(200.0f, 24.0f))) {
                saveDisplayPrefs();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Viewport Specific Controls");

            int activeViewportCount = 1;
            if (viz_.layout == ScreenLayout::SplitLeftRight || viz_.layout == ScreenLayout::SplitTopBottom) activeViewportCount = 2;
            else if (viz_.layout == ScreenLayout::Quad) activeViewportCount = 4;

            viz_.activeViewportEditor = std::clamp(viz_.activeViewportEditor, 0, activeViewportCount - 1);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
            for (int i = 0; i < activeViewportCount; ++i) {
                if (i > 0) ImGui::SameLine();
                std::string btnLabel = "Display " + std::to_string(i + 1);

                bool isActive = (viz_.activeViewportEditor == i);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
                }
                if (ImGui::Button(btnLabel.c_str(), ImVec2(90, 24))) {
                    viz_.activeViewportEditor = i;
                }
                if (isActive) {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::PopStyleVar();

            ImGui::Spacing();

            auto& vp = viz_.viewports[viz_.activeViewportEditor];

            int displayType = static_cast<int>(vp.displayType);
            if (ImGui::Combo("Viewport Display Type", &displayType, displayTypeNames.data(), static_cast<int>(displayTypeNames.size()))) {
                vp.displayType = static_cast<DisplayType>(std::clamp(displayType, 0, static_cast<int>(displayTypeNames.size()) - 1));
            }
            settingHint("Choose how this viewport interprets data: raw scalar, categories, relative elevation, or surface water.");

            if (!viz_.fieldNames.empty()) {
                clampVisualizationIndices();
                if (ImGui::BeginCombo("Primary Field", viz_.fieldNames[static_cast<std::size_t>(vp.primaryFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        const bool selected = (vp.primaryFieldIndex == i);
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), selected)) {
                            vp.primaryFieldIndex = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                settingHint("Main scalar variable displayed in this specific panel.");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Colorization & Ranges");

            static constexpr std::array<const char*, 4> colorMapNames = {"Turbo", "Grayscale", "Diverging", "Water"};
            int colorMap = static_cast<int>(vp.colorMapMode);
            if (ImGui::Combo("Color Palette", &colorMap, colorMapNames.data(), static_cast<int>(colorMapNames.size()))) {
                vp.colorMapMode = static_cast<ColorMapMode>(std::clamp(colorMap, 0, static_cast<int>(colorMapNames.size()) - 1));
            }
            settingHint("Transfer palette used to map normalized scalar values for this view.");

            static constexpr std::array<const char*, 3> normalizationNames = {"Per-frame Auto", "Sticky Limit (per-field)", "Fixed Manual Bounds"};
            int normalization = static_cast<int>(vp.normalizationMode);
            if (ImGui::Combo("Normalization", &normalization, normalizationNames.data(), static_cast<int>(normalizationNames.size()))) {
                vp.normalizationMode = static_cast<NormalizationMode>(std::clamp(normalization, 0, static_cast<int>(normalizationNames.size()) - 1));
            }
            settingHint("Controls how value ranges are normalized before color mapping for this specific view.");

            if (vp.displayType != DisplayType::ScalarField) {
                ImGui::TextDisabled("Category display type selected: scalar normalization/palette are secondary.");
            }

            if (vp.normalizationMode == NormalizationMode::StickyPerField) {
                if (ImGui::Button("Reset Sticky Tracking", ImVec2(170.0f, 24.0f))) {
                    vp.stickyRanges.clear();
                }
                ImGui::SameLine();
                checkboxWithHint("Debug range metrics", &vp.showRangeDetails, "Shows min/max diagnostics for normalization debugging.");
            } else if (vp.normalizationMode == NormalizationMode::FixedManual) {
                sliderFloatWithHint("Range Min", &vp.fixedRangeMin, -15.0f, 15.0f, "%.3f", "Lower bound for fixed normalization mode.");
                sliderFloatWithHint("Range Max", &vp.fixedRangeMax, -15.0f, 15.0f, "%.3f", "Upper bound for fixed normalization mode.");
            }

            checkboxWithHint("Show Legend on View", &vp.showLegend, "Displays min/max/value legend overlays where available on this display.");

            ImGui::Separator();
            ImGui::TextUnformatted("Data Visual Overlays");

            checkboxWithHint("Render Vector Overlay", &vp.showVectorField, "Draws vectors from the selected X/Y scalar fields over this specific Viewport.");
            if (vp.showVectorField && !viz_.fieldNames.empty()) {
                ImGui::Indent();
                if (ImGui::BeginCombo("Def. X", viz_.fieldNames[static_cast<std::size_t>(vp.vectorXFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), vp.vectorXFieldIndex == i)) { vp.vectorXFieldIndex = i; }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Def. Y", viz_.fieldNames[static_cast<std::size_t>(vp.vectorYFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), vp.vectorYFieldIndex == i)) { vp.vectorYFieldIndex = i; }
                    }
                    ImGui::EndCombo();
                }
                sliderIntWithHint("Ray Stride", &vp.vectorStride, 1, 32, "Sample spacing used when drawing vectors.");
                sliderFloatWithHint("Ray Scale", &vp.vectorScale, 0.05f, 2.0f, "%.2f", "Vector length scale.");
                ImGui::Unindent();
            }
        }
        PopSectionTint();
    }

    void drawOverlaysSection() {
        PushSectionTint(4);
        if (ImGui::CollapsingHeader("Global Spatial Overlays", 0)) {
            checkboxWithHint("Show Domain Boundary", &visuals_.showBoundary, "Draw a boundary rectangle around the visible simulation domain.");
            if (visuals_.showBoundary) {
                ImGui::Indent();
                sliderFloatWithHint("Opacity", &visuals_.boundaryOpacity, 0.0f, 1.0f, "%.2f", "Boundary line alpha.");
                sliderFloatWithHint("Thickness", &visuals_.boundaryThickness, 0.5f, 6.0f, "%.2f", "Boundary line width in pixels.");
                checkboxWithHint("Animate Pulse", &visuals_.boundaryAnimate, "Animates boundary opacity to improve visibility during motion.");
                if (accessibility_.reduceMotion) { visuals_.boundaryAnimate = false; }
                ImGui::Unindent();
            }

            checkboxWithHint("Overlay Cell Grid", &visuals_.showGrid, "Draws grid lines over rasterized cells across all displays.");
            if (visuals_.showGrid) {
                viz_.showCellGrid = true;
                ImGui::Indent();
                sliderFloatWithHint("Grid Op", &visuals_.gridOpacity, 0.0f, 1.0f, "%.2f", "Grid line opacity.");
                sliderFloatWithHint("Grid W", &visuals_.gridLineThickness, 0.5f, 4.0f, "%.2f", "Grid line thickness in pixels.");
                ImGui::Unindent();
            } else {
                viz_.showCellGrid = false;
            }

            ImGui::Separator();
            checkboxWithHint("Render Sparse Objects", &viz_.showSparseOverlay, "Includes sparse overlay entries in the rasterized display.");
        }
        PopSectionTint();
    }

    void drawOpticsSection() {
        PushSectionTint(5);
        if (ImGui::CollapsingHeader("Camera & Optics", 0)) {
            sliderFloatWithHint("Zoom", &visuals_.zoom, 0.1f, 10.0f, "%.2f", "Zoom factor while preserving aspect ratio.");
            sliderFloatWithHint("Pan X", &visuals_.panX, -2000.0f, 2000.0f, "%.1f", "Horizontal pan offset in pixels.");
            sliderFloatWithHint("Pan Y", &visuals_.panY, -2000.0f, 2000.0f, "%.1f", "Vertical pan offset in pixels.");
            ImGui::Separator();
            sliderFloatWithHint("Brightness", &visuals_.brightness, 0.1f, 3.0f, "%.2f", "Post-color brightness multiplier.");
            sliderFloatWithHint("Contrast", &visuals_.contrast, 0.1f, 3.0f, "%.2f", "Post-color contrast around mid-gray.");
            sliderFloatWithHint("Gamma", &visuals_.gamma, 0.2f, 3.0f, "%.2f", "Display gamma correction applied after contrast/brightness.");
            checkboxWithHint("Invert Colors", &visuals_.invertColors, "Inverts mapped colors after transfer.");
        }
        PopSectionTint();
    }

    void drawGridSetupSection() {
        PushSectionTint(6);
        if (ImGui::CollapsingHeader("Grid Matrix Registration", ImGuiTreeNodeFlags_DefaultOpen)) {
            checkboxWithHint("Manual seed", &panel_.useManualSeed, "When disabled, a fresh random seed is generated at world creation.");
            if (panel_.useManualSeed) {
                std::uint64_t manualSeed = panel_.seed;
                if (ImGui::InputScalar("Seed", ImGuiDataType_U64, &manualSeed)) {
                    panel_.seed = std::max<std::uint64_t>(1ull, manualSeed);
                }
                settingHint("Manual deterministic seed. Same seed + same params reproduce the same world.");
            } else {
                ImGui::Text("Seed (auto): %llu", static_cast<unsigned long long>(panel_.seed));
                if (SecondaryButton("Reroll Seed", ImVec2(140.0f, 24.0f))) {
                    panel_.seed = generateRandomSeed();
                }
                settingHint("Auto mode uses a randomized seed on each creation. Reroll previews another one.");
            }

            sliderIntWithHint("Width (Cols)", &panel_.gridWidth, 1, 4096, "Grid width in cells (max 4096).");
            sliderIntWithHint("Height (Rows)", &panel_.gridHeight, 1, 4096, "Grid height in cells (max 4096).");

            if (ImGui::BeginCombo("Runtime Tier", kTierOptions[panel_.tierIndex])) {
                for (int i = 0; i < static_cast<int>(kTierOptions.size()); ++i) {
                    if (ImGui::Selectable(kTierOptions[i], panel_.tierIndex == i)) { panel_.tierIndex = i; }
                }
                ImGui::EndCombo();
            }
            settingHint("Model family tier controlling coupling complexity:\n"
                        "A (Baseline): Simple interactions, fast performance.\n"
                        "B (Intermediate): Balanced coupling rules.\n"
                        "C (Advanced): High realism, intricate non-linear dependencies.");

            if (ImGui::BeginCombo("Temporal Mode", kTemporalOptions[panel_.temporalIndex])) {
                for (int i = 0; i < static_cast<int>(kTemporalOptions.size()); ++i) {
                    if (ImGui::Selectable(kTemporalOptions[i], panel_.temporalIndex == i)) { panel_.temporalIndex = i; }
                }
                ImGui::EndCombo();
            }
            settingHint("Temporal integration policy used by the scheduler.");

            ImGui::TextDisabled("These values are applied when you finalize world creation.");
        }
        PopSectionTint();
    }

    void drawWorldGenerationSection() {
        PushSectionTint(7);
        if (ImGui::CollapsingHeader("World Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted("Terrain Spectrum");
            sliderFloatWithHint("Base Frequency", &panel_.terrainBaseFrequency, 0.1f, 12.0f, "%.2f", "Low-frequency terrain structure scale (continents/hills).");
            sliderFloatWithHint("Detail Frequency", &panel_.terrainDetailFrequency, 0.2f, 24.0f, "%.2f", "High-frequency terrain detail scale.");
            sliderFloatWithHint("Warp Strength", &panel_.terrainWarpStrength, 0.0f, 2.0f, "%.2f", "Domain warp amount used to bend noise patterns.");
            sliderFloatWithHint("Terrain Amplitude", &panel_.terrainAmplitude, 0.1f, 3.0f, "%.2f", "Overall elevation contrast.");
            sliderFloatWithHint("Ridge Mix", &panel_.terrainRidgeMix, 0.0f, 1.0f, "%.2f", "Adds ridge-like structures to terrain.");

            ImGui::Separator();
            ImGui::TextUnformatted("Noise Fractal Parameters (Advanced)");
            int octaves = panel_.terrainOctaves;
            if (sliderIntWithHint("Octaves", &octaves, 1, 8, "Number of detail layers added together.")) {
                panel_.terrainOctaves = octaves;
            }
            sliderFloatWithHint("Lacunarity", &panel_.terrainLacunarity, 1.0f, 4.0f, "%.2f", "Frequency multiplier per octave (usually ~2.0).");
            sliderFloatWithHint("Gain", &panel_.terrainGain, 0.1f, 1.0f, "%.2f", "Amplitude multiplier per octave (usually ~0.5).");

            ImGui::Separator();
            ImGui::TextUnformatted("Climate and Hydrology");
            sliderFloatWithHint("Latitude Banding", &panel_.latitudeBanding, 0.0f, 2.0f, "%.2f", "Strength of dominant horizontal climate bands from Equator to Poles.");
            sliderFloatWithHint("Sea Level", &panel_.seaLevel, 0.0f, 1.0f, "%.3f", "Waterline threshold used to derive initial water coverage.");
            sliderFloatWithHint("Polar Cooling", &panel_.polarCooling, 0.0f, 1.5f, "%.2f", "Strength of temperature cooling away from warm latitudes.");
            sliderFloatWithHint("Humidity from Water", &panel_.humidityFromWater, 0.0f, 1.5f, "%.2f", "Influence of water presence on humidity initialization.");
            sliderFloatWithHint("Biome Noise", &panel_.biomeNoiseStrength, 0.0f, 1.0f, "%.2f", "Additional noise contribution to biome/temperature diversity.");

            ImGui::Separator();
            ImGui::TextUnformatted("Island Morphology (New)");
            sliderFloatWithHint("Island Density", &panel_.islandDensity, 0.05f, 0.95f, "%.3f", "Controls how many islands are generated across the map.");
            sliderFloatWithHint("Island Falloff", &panel_.islandFalloff, 0.35f, 4.5f, "%.2f", "Controls island shape falloff from center to coast.");
            sliderFloatWithHint("Coastline Sharpness", &panel_.coastlineSharpness, 0.25f, 4.0f, "%.2f", "Controls coastline transition sharpness.");
            sliderFloatWithHint("Archipelago Jitter", &panel_.archipelagoJitter, 0.0f, 1.5f, "%.2f", "Jitters island centers to break regular chunks.");
            sliderFloatWithHint("Erosion Strength", &panel_.erosionStrength, 0.0f, 1.0f, "%.2f", "Applies erosion-like modulation to terrain smoothing.");
            sliderFloatWithHint("Shelf Depth", &panel_.shelfDepth, 0.0f, 0.8f, "%.2f", "Controls continental shelf depth around coasts.");

            ImGui::Separator();
            ImGui::TextUnformatted("Display Type Manager");
            static constexpr std::array<const char*, 4> displayTypeNames = {
                "Scalar Field",
                "Surface Category (water/land/highland)",
                "Relative Elevation",
                "Surface Water (dry=black)"
            };
            int mode = static_cast<int>(viz_.generationPreviewDisplayType);
            if (ImGui::Combo("Preview Display Type", &mode, displayTypeNames.data(), static_cast<int>(displayTypeNames.size()))) {
                viz_.generationPreviewDisplayType = static_cast<DisplayType>(std::clamp(mode, 0, static_cast<int>(displayTypeNames.size()) - 1));
            }
            settingHint("Uses the same display interpretation manager as simulation viewports.");

            checkboxWithHint("Auto Water Level", &viz_.displayManager.autoWaterLevel, "Automatically derives display waterline from terrain values.");
            if (viz_.displayManager.autoWaterLevel) {
                sliderFloatWithHint("Auto Water Quantile", &viz_.displayManager.autoWaterQuantile, 0.10f, 0.90f, "%.3f", "Quantile used for automatic waterline inference.");
            } else {
                sliderFloatWithHint("Manual Waterline", &viz_.displayManager.waterLevel, 0.0f, 1.0f, "%.3f", "Manual shared threshold for water/land classification.");
            }
            sliderFloatWithHint("Lowland Threshold", &viz_.displayManager.lowlandThreshold, 0.0f, 1.0f, "%.3f", "Shared breakpoint between lowland and upland.");
            sliderFloatWithHint("Highland Threshold", &viz_.displayManager.highlandThreshold, 0.0f, 1.0f, "%.3f", "Shared breakpoint for highland/mountain categories.");
            sliderFloatWithHint("Water Presence Threshold", &viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f, "%.3f", "Minimum water signal before a cell is rendered as water.");
            viz_.displayManager.waterLevel = std::clamp(viz_.displayManager.waterLevel, 0.0f, 1.0f);
            viz_.displayManager.autoWaterQuantile = std::clamp(viz_.displayManager.autoWaterQuantile, 0.0f, 1.0f);
            viz_.displayManager.lowlandThreshold = std::clamp(viz_.displayManager.lowlandThreshold, 0.0f, 1.0f);
            viz_.displayManager.highlandThreshold = std::clamp(viz_.displayManager.highlandThreshold, viz_.displayManager.lowlandThreshold + 0.01f, 1.0f);
            viz_.displayManager.waterPresenceThreshold = std::clamp(viz_.displayManager.waterPresenceThreshold, 0.0f, 1.0f);

            ImGui::Spacing();
            ImGui::TextDisabled("Preview updates from these parameters without running simulation.");
        }
        PopSectionTint();
    }

    void drawPhysicsSection() {
        PushSectionTint(8);
        if (ImGui::CollapsingHeader("Environmental Physics", 0)) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Continuum Force Models");
            sliderFloatWithHint("Global Force Scale", &panel_.forceFieldScale, 0.0f, 10.0f, "%.2f", "Strength of force-field interactions.");
            sliderFloatWithHint("Kinetic Damping", &panel_.forceFieldDamping, 0.0f, 1.0f, "%.3f", "Velocity damping factor per step.");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Particulate Interactions");
            sliderFloatWithHint("Entity Mobility", &panel_.particleMobility, 0.0f, 1.0f, "%.3f", "Mobility factor for particle-like transport behavior.");
            sliderFloatWithHint("Cluster Cohesion", &panel_.particleCohesion, 0.0f, 1.0f, "%.3f", "Tendency to cluster with neighboring state.");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Numerical Bounds");
            sliderFloatWithHint("Boundary Rigidity", &panel_.constraintRigidity, 0.0f, 1.0f, "%.3f", "Strength of imposed boundary constraints.");
            sliderFloatWithHint("Solver Tolerance", &panel_.constraintTolerance, 0.0f, 1.0f, "%.3f", "Accepted numerical tolerance for constraint resolution.");
        }
        PopSectionTint();
    }

    void drawAnalysisSection() {
        PushSectionTint(8);
        if (ImGui::CollapsingHeader("Data Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (PrimaryButton("Dump State", ImVec2(90, 26))) {
                std::string message;
                runtime_.status(message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Metrics", ImVec2(90, 26))) {
                std::string message;
                runtime_.metrics(message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Index", ImVec2(90, 26))) {
                std::string message;
                runtime_.listFields(message);
                appendLog(message);
            }

            ImGui::Spacing();
            inputTextWithHint("Query Variable", panel_.summaryVariable, sizeof(panel_.summaryVariable), "Field name to summarize (e.g., temperature_T).");
            if (PrimaryButton("Extract Summary", ImVec2(-1.0f, 26))) {
                std::string message;
                runtime_.summarizeField(panel_.summaryVariable, message);
                appendLog(message);
            }

            ImGui::Separator();
            ImGui::Text("Volume Checkpoints");
            inputTextWithHint("Label", panel_.checkpointLabel, sizeof(panel_.checkpointLabel), "Checkpoint label used by store/restore/list operations.");

            if (PrimaryButton("Store", ImVec2(90, 26))) {
                std::string message;
                runtime_.createCheckpoint(panel_.checkpointLabel, message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Restore", ImVec2(90, 26))) {
                std::string message;
                runtime_.restoreCheckpoint(panel_.checkpointLabel, message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("List", ImVec2(90, 26))) {
                std::string message;
                runtime_.listCheckpoints(message);
                appendLog(message);
            }
        }
        PopSectionTint();
    }

    void drawAccessibilitySection() {
        PushSectionTint(9);
        if (ImGui::CollapsingHeader("Cockpit Accessibility", 0)) {
            bool styleChanged = false;
            bool rebuildFonts = false;

            styleChanged |= sliderFloatWithHint("UI Scale", &accessibility_.uiScale, 0.75f, 3.0f, "%.2f", "Global UI scale multiplier.");
            if (sliderFloatWithHint("Font Size", &accessibility_.fontSizePx, 10.0f, 32.0f, "%.1f", "Base font pixel size.")) {
                styleChanged = true;
                rebuildFonts = true;
            }

            styleChanged |= checkboxWithHint("High Contrast", &accessibility_.highContrast, "Boosts contrast in the UI theme.");
            styleChanged |= checkboxWithHint("Keyboard Navigation", &accessibility_.keyboardNav, "Enables keyboard-first UI navigation.");
            styleChanged |= checkboxWithHint("Focus Indicators", &accessibility_.focusIndicators, "Highlights active/focused controls.");
            checkboxWithHint("Reduce Motion", &accessibility_.reduceMotion, "Disables non-essential animation effects.");

            if (styleChanged) {
                applyTheme(rebuildFonts);
            }
        }
        PopSectionTint();
    }

    void syncPanelFromConfig() {
        const auto& config = runtime_.config();
        panel_.seed = config.seed;
        panel_.useManualSeed = true;
        panel_.gridWidth = static_cast<int>(config.grid.width);
        panel_.gridHeight = static_cast<int>(config.grid.height);
        panel_.tierIndex = (config.tier == ModelTier::A) ? 0 : (config.tier == ModelTier::B) ? 1 : 2;

        const std::string temporal = app::temporalPolicyToString(config.temporalPolicy);
        panel_.temporalIndex = (temporal == "uniform") ? 0 : (temporal == "phased") ? 1 : 2;

        panel_.terrainBaseFrequency = config.worldGen.terrainBaseFrequency;
        panel_.terrainDetailFrequency = config.worldGen.terrainDetailFrequency;
        panel_.terrainWarpStrength = config.worldGen.terrainWarpStrength;
        panel_.terrainAmplitude = config.worldGen.terrainAmplitude;
        panel_.terrainRidgeMix = config.worldGen.terrainRidgeMix;
        panel_.terrainOctaves = config.worldGen.terrainOctaves;
        panel_.terrainLacunarity = config.worldGen.terrainLacunarity;
        panel_.terrainGain = config.worldGen.terrainGain;
        panel_.seaLevel = config.worldGen.seaLevel;
        panel_.polarCooling = config.worldGen.polarCooling;
        panel_.latitudeBanding = config.worldGen.latitudeBanding;
        panel_.humidityFromWater = config.worldGen.humidityFromWater;
        panel_.biomeNoiseStrength = config.worldGen.biomeNoiseStrength;
        panel_.islandDensity = config.worldGen.islandDensity;
        panel_.islandFalloff = config.worldGen.islandFalloff;
        panel_.coastlineSharpness = config.worldGen.coastlineSharpness;
        panel_.archipelagoJitter = config.worldGen.archipelagoJitter;
        panel_.erosionStrength = config.worldGen.erosionStrength;
        panel_.shelfDepth = config.worldGen.shelfDepth;
    }

    void applyConfigFromPanel() {
        app::LaunchConfig config = runtime_.config();
        config.seed = panel_.seed;
        config.grid = GridSpec{
            static_cast<std::uint32_t>(std::clamp(panel_.gridWidth, 1, 4096)),
            static_cast<std::uint32_t>(std::clamp(panel_.gridHeight, 1, 4096))};
        config.tier = (panel_.tierIndex == 0) ? ModelTier::A : (panel_.tierIndex == 1) ? ModelTier::B : ModelTier::C;
        const auto parsedTemporal = app::parseTemporalPolicy(kTemporalOptions[panel_.temporalIndex]);
        if (parsedTemporal.has_value()) {
            config.temporalPolicy = *parsedTemporal;
        }

        config.worldGen.terrainBaseFrequency = panel_.terrainBaseFrequency;
        config.worldGen.terrainDetailFrequency = panel_.terrainDetailFrequency;
        config.worldGen.terrainWarpStrength = panel_.terrainWarpStrength;
        config.worldGen.terrainAmplitude = panel_.terrainAmplitude;
        config.worldGen.terrainRidgeMix = panel_.terrainRidgeMix;
        config.worldGen.terrainOctaves = panel_.terrainOctaves;
        config.worldGen.terrainLacunarity = panel_.terrainLacunarity;
        config.worldGen.terrainGain = panel_.terrainGain;
        config.worldGen.seaLevel = panel_.seaLevel;
        config.worldGen.polarCooling = panel_.polarCooling;
        config.worldGen.latitudeBanding = panel_.latitudeBanding;
        config.worldGen.humidityFromWater = panel_.humidityFromWater;
        config.worldGen.biomeNoiseStrength = panel_.biomeNoiseStrength;
        config.worldGen.islandDensity = panel_.islandDensity;
        config.worldGen.islandFalloff = panel_.islandFalloff;
        config.worldGen.coastlineSharpness = panel_.coastlineSharpness;
        config.worldGen.archipelagoJitter = panel_.archipelagoJitter;
        config.worldGen.erosionStrength = panel_.erosionStrength;
        config.worldGen.shelfDepth = panel_.shelfDepth;

        runtime_.setConfig(config);

        std::ostringstream output;
        output << "config_applied seed=" << config.seed
               << " grid=" << config.grid.width << 'x' << config.grid.height
               << " tier=" << toString(config.tier)
               << " temporal=" << app::temporalPolicyToString(config.temporalPolicy)
               << " gen.base_freq=" << config.worldGen.terrainBaseFrequency
               << " gen.detail_freq=" << config.worldGen.terrainDetailFrequency
               << " gen.sea_level=" << config.worldGen.seaLevel
               << " gen.island_density=" << config.worldGen.islandDensity
               << " gen.coast_sharpness=" << config.worldGen.coastlineSharpness;
        appendLog(output.str());
        requestSnapshotRefresh();
    }

    void triggerOverlay(const OverlayIcon icon) {
        overlay_.icon = icon;
        overlay_.alpha = 1.0f;
    }

    void enterSimulationPaused() {
        viz_.autoRun = false;
        if (runtime_.isRunning() && !runtime_.isPaused()) {
            std::string message;
            if (runtime_.pause(message)) {
                appendLog(message);
            } else if (!message.empty()) {
                appendLog(message);
            }
        }
        requestSnapshotRefresh();
        appState_ = AppState::Simulation;
        triggerOverlay(OverlayIcon::Pause);
    }

    void appendLog(const std::string& line) {
        if (!line.empty()) {
            logs_.push_back(line);
        }
        if (logs_.size() > 1000) {
            logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 1000));
        }
    }

#endif // WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT
