#include "ws/gui/main_window.hpp"

#include "ws/gui/runtime_service.hpp"
#include "ws/gui/theme_bootstrap.hpp"
#include "ws/gui/ui_components.hpp"

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace ws::gui {
namespace {

enum class OverlayIcon {
    None,
    Play,
    Pause
};

struct VisualParams {
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float gamma = 1.0f;
    bool invertColors = false;

    bool showGrid = true;
    float gridOpacity = 0.35f;
    float gridLineThickness = 1.0f;

    bool showBoundary = true;
    float boundaryOpacity = 0.85f;
    float boundaryThickness = 1.2f;
    bool boundaryAnimate = true;
};

struct PanelState {
    std::uint64_t seed = 42;
    int gridWidth = 32;
    int gridHeight = 16;
    int tierIndex = 0;
    int temporalIndex = 0;

    int stepCount = 1;
    std::uint64_t runUntilTarget = 100;

    float forceFieldScale = 1.0f;
    float forceFieldDamping = 0.15f;
    float particleMobility = 0.6f;
    float particleCohesion = 0.2f;
    float constraintRigidity = 0.5f;
    float constraintTolerance = 0.1f;

    char profileName[128] = "baseline";
    char summaryVariable[128] = "temperature_t";
    char checkpointLabel[128] = "quick";
};

struct OverlayState {
    float alpha = 0.0f;
    OverlayIcon icon = OverlayIcon::None;
};

constexpr std::array<const char*, 3> kTierOptions = {"A", "B", "C"};
constexpr std::array<const char*, 3> kTemporalOptions = {"uniform", "phased", "multirate"};
constexpr int kImGuiIntSafeMax = std::numeric_limits<int>::max() / 2;

GLFWwindow* createGlfwWindowWithFallback() {
    struct VersionProfile {
        int major;
        int minor;
    };

    constexpr std::array<VersionProfile, 3> profiles = {
        VersionProfile{4, 5},
        VersionProfile{4, 1},
        VersionProfile{3, 3}
    };

    for (const auto& profile : profiles) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, profile.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, profile.minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* window = glfwCreateWindow(1600, 960, "World Simulator - Control Cockpit", nullptr, nullptr);
        if (window != nullptr) {
            return window;
        }
    }

    return nullptr;
}

void drawPlaybackOverlay(OverlayState& overlay, const bool reduceMotion, const float deltaTime) {
    if (reduceMotion) {
        overlay.alpha = 0.0f;
        return;
    }

    overlay.alpha = std::max(0.0f, overlay.alpha - (1.2f * deltaTime));
    if (overlay.alpha <= 0.0f || overlay.icon == OverlayIcon::None) {
        return;
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 center(display.x - 44.0f, 44.0f);
    const float radius = 22.0f;
    const int alpha = static_cast<int>(overlay.alpha * 255.0f);
    dl->AddCircleFilled(center, radius, IM_COL32(35, 45, 70, alpha));
    dl->AddCircle(center, radius, IM_COL32(130, 170, 255, alpha), 24, 2.0f);

    if (overlay.icon == OverlayIcon::Play) {
        dl->AddTriangleFilled(
            ImVec2(center.x - 6.0f, center.y - 8.0f),
            ImVec2(center.x - 6.0f, center.y + 8.0f),
            ImVec2(center.x + 9.0f, center.y),
            IM_COL32(240, 245, 255, alpha));
    } else if (overlay.icon == OverlayIcon::Pause) {
        dl->AddRectFilled(ImVec2(center.x - 8.0f, center.y - 8.0f), ImVec2(center.x - 2.0f, center.y + 8.0f), IM_COL32(240, 245, 255, alpha));
        dl->AddRectFilled(ImVec2(center.x + 2.0f, center.y - 8.0f), ImVec2(center.x + 8.0f, center.y + 8.0f), IM_COL32(240, 245, 255, alpha));
    }
}

class MainWindowImpl {
public:
    int run() {
        if (glfwInit() != GLFW_TRUE) {
            return 1;
        }

        GLFWwindow* window = createGlfwWindowWithFallback();
        if (!window) {
            glfwTerminate();
            return 1;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        ThemeBootstrap::applyBaseTheme(ImGui::GetStyle(), accessibility_.uiScale);
        ThemeBootstrap::configureFont(io, accessibility_.fontSizePx);
        ThemeBootstrap::applyAccessibility(io, ImGui::GetStyle(), accessibility_);

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 450");

        std::string startupMessage;
        runtime_.start(startupMessage);
        appendLog(startupMessage);

        syncPanelFromConfig();

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            drawViewport();
            drawDockSpace();
            drawControlPanel();
            drawPlaybackOverlay(overlay_, accessibility_.reduceMotion, ImGui::GetIO().DeltaTime);

            ImGui::Render();

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            glViewport(0, 0, framebufferWidth, framebufferHeight);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }

private:
    void applyTheme(const bool rebuildFonts) {
        ImGuiStyle& style = ImGui::GetStyle();
        ThemeBootstrap::applyBaseTheme(style, accessibility_.uiScale);
        ThemeBootstrap::applyAccessibility(ImGui::GetIO(), style, accessibility_);
        if (rebuildFonts) {
            ThemeBootstrap::configureFont(ImGui::GetIO(), accessibility_.fontSizePx);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui::GetIO().Fonts->Build();
            ImGui_ImplOpenGL3_CreateFontsTexture();
        }
    }

    void drawViewport() {
        float base = 0.05f * visuals_.brightness;
        float b = std::clamp(base * visuals_.contrast, 0.0f, 1.0f);
        if (visuals_.invertColors) {
            b = 1.0f - b;
        }

        const float r = std::clamp(b * 0.90f, 0.0f, 1.0f);
        const float g = std::clamp(b * 0.95f, 0.0f, 1.0f);
        const float c = std::clamp(b * 1.15f, 0.0f, 1.0f);
        glClearColor(r, g, c, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
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
        ImGui::PopStyleVar(3);
        ImGui::End();
    }

    void drawControlPanel() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 920.0f), ImGuiCond_FirstUseEver);

        ImGui::Begin("Control Panel");

        drawInfoSection();
        drawPerformanceSection();
        drawGridSection();
        drawToolsSection();
        drawPresetsSection();
        drawSimulationSection();
        drawForceFieldsSection();
        drawParticlePropertiesSection();
        drawConstraintsSection();
        drawDisplaySection();
        drawAnalysisSection();
        drawAccessibilitySection();
        drawLogSection();

        ImGui::End();
    }

    void drawInfoSection() {
        PushSectionTint(0);
        if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            StatusBadge(runtime_.isRunning() ? "RUNNING" : "STOPPED", runtime_.isRunning());
            ImGui::SameLine();
            StatusBadge(runtime_.isPaused() ? "PAUSED" : "ACTIVE", !runtime_.isPaused());
            ImGui::Text("Cockpit UI: ImGui + GLFW + OpenGL 4.5");
        }
        PopSectionTint();
    }

    void drawPerformanceSection() {
        PushSectionTint(1);
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            DelayedTooltip("Immediate-mode pipeline metrics.");
        }
        PopSectionTint();
    }

    void drawGridSection() {
        PushSectionTint(2);
        if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
            int seed = static_cast<int>(std::min<std::uint64_t>(panel_.seed, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
            if (NumericSliderPairInt("Seed", &seed, 0, kImGuiIntSafeMax)) {
                panel_.seed = static_cast<std::uint64_t>(seed);
            }

            NumericSliderPairInt("Grid Width", &panel_.gridWidth, 1, 1024);
            NumericSliderPairInt("Grid Height", &panel_.gridHeight, 1, 1024);

            if (ImGui::BeginCombo("Tier", kTierOptions[panel_.tierIndex])) {
                for (int i = 0; i < static_cast<int>(kTierOptions.size()); ++i) {
                    const bool selected = (panel_.tierIndex == i);
                    if (ImGui::Selectable(kTierOptions[i], selected)) {
                        panel_.tierIndex = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo("Temporal", kTemporalOptions[panel_.temporalIndex])) {
                for (int i = 0; i < static_cast<int>(kTemporalOptions.size()); ++i) {
                    const bool selected = (panel_.temporalIndex == i);
                    if (ImGui::Selectable(kTemporalOptions[i], selected)) {
                        panel_.temporalIndex = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (PrimaryButton("Apply Grid Config", ImVec2(-1.0f, 28.0f))) {
                applyConfigFromPanel();
            }
        }
        PopSectionTint();
    }

    void drawToolsSection() {
        PushSectionTint(3);
        if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (PrimaryButton("Start", ImVec2(100, 28))) {
                std::string message;
                runtime_.start(message);
                appendLog(message);
                triggerOverlay(OverlayIcon::Play);
            }
            ImGui::SameLine();
            if (PrimaryButton("Stop", ImVec2(100, 28))) {
                std::string message;
                runtime_.stop(message);
                appendLog(message);
                triggerOverlay(OverlayIcon::Pause);
            }
            ImGui::SameLine();
            if (PrimaryButton("Restart", ImVec2(100, 28))) {
                std::string message;
                runtime_.restart(message);
                appendLog(message);
                triggerOverlay(OverlayIcon::Play);
            }
        }
        PopSectionTint();
    }

    void drawPresetsSection() {
        PushSectionTint(4);
        if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("Profile", panel_.profileName, sizeof(panel_.profileName));
            if (PrimaryButton("Save Profile", ImVec2(120, 26))) {
                std::string message;
                runtime_.saveProfile(panel_.profileName, message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Load Profile", ImVec2(120, 26))) {
                std::string message;
                runtime_.loadProfile(panel_.profileName, message);
                appendLog(message);
                syncPanelFromConfig();
            }
            ImGui::SameLine();
            if (PrimaryButton("List", ImVec2(80, 26))) {
                std::string message;
                runtime_.listProfiles(message);
                appendLog(message);
            }
        }
        PopSectionTint();
    }

    void drawSimulationSection() {
        PushSectionTint(5);
        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            NumericSliderPairInt("Step Count", &panel_.stepCount, 1, 1000000);

            int runUntil = static_cast<int>(std::min<std::uint64_t>(panel_.runUntilTarget, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
            if (NumericSliderPairInt("Run Until", &runUntil, 0, kImGuiIntSafeMax)) {
                panel_.runUntilTarget = static_cast<std::uint64_t>(runUntil);
            }

            if (PrimaryButton("Run Step", ImVec2(100, 26))) {
                std::string message;
                runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Run Until", ImVec2(100, 26))) {
                std::string message;
                runtime_.runUntil(panel_.runUntilTarget, message);
                appendLog(message);
            }
            ImGui::SameLine();
            if (PrimaryButton("Pause", ImVec2(80, 26))) {
                std::string message;
                runtime_.pause(message);
                appendLog(message);
                triggerOverlay(OverlayIcon::Pause);
            }
            ImGui::SameLine();
            if (PrimaryButton("Resume", ImVec2(80, 26))) {
                std::string message;
                runtime_.resume(message);
                appendLog(message);
                triggerOverlay(OverlayIcon::Play);
            }
        }
        PopSectionTint();
    }

    void drawForceFieldsSection() {
        PushSectionTint(6);
        if (ImGui::CollapsingHeader("Force Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
            NumericSliderPair("Force Scale", &panel_.forceFieldScale, 0.0f, 10.0f, "%.2f");
            NumericSliderPair("Damping", &panel_.forceFieldDamping, 0.0f, 1.0f, "%.3f");
        }
        PopSectionTint();
    }

    void drawParticlePropertiesSection() {
        PushSectionTint(7);
        if (ImGui::CollapsingHeader("Particle Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            NumericSliderPair("Mobility", &panel_.particleMobility, 0.0f, 1.0f, "%.3f");
            NumericSliderPair("Cohesion", &panel_.particleCohesion, 0.0f, 1.0f, "%.3f");
        }
        PopSectionTint();
    }

    void drawConstraintsSection() {
        PushSectionTint(8);
        if (ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
            NumericSliderPair("Rigidity", &panel_.constraintRigidity, 0.0f, 1.0f, "%.3f");
            NumericSliderPair("Tolerance", &panel_.constraintTolerance, 0.0f, 1.0f, "%.3f");
        }
        PopSectionTint();
    }

    void drawDisplaySection() {
        PushSectionTint(9);
        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            NumericSliderPair("Zoom", &visuals_.zoom, 0.1f, 5.0f, "%.2f");
            NumericSliderPair("Pan X", &visuals_.panX, -1000.0f, 1000.0f, "%.1f");
            NumericSliderPair("Pan Y", &visuals_.panY, -1000.0f, 1000.0f, "%.1f");
            NumericSliderPair("Brightness", &visuals_.brightness, 0.1f, 3.0f, "%.2f");
            NumericSliderPair("Contrast", &visuals_.contrast, 0.1f, 3.0f, "%.2f");
            NumericSliderPair("Gamma", &visuals_.gamma, 0.2f, 3.0f, "%.2f");
            ImGui::Checkbox("Invert Colors", &visuals_.invertColors);

            ImGui::Separator();
            ImGui::Checkbox("Show Grid", &visuals_.showGrid);
            if (visuals_.showGrid) {
                NumericSliderPair("Grid Opacity", &visuals_.gridOpacity, 0.0f, 1.0f, "%.2f");
                NumericSliderPair("Grid Thickness", &visuals_.gridLineThickness, 0.5f, 4.0f, "%.2f");
            }

            ImGui::Checkbox("Show Boundary", &visuals_.showBoundary);
            if (visuals_.showBoundary) {
                NumericSliderPair("Boundary Opacity", &visuals_.boundaryOpacity, 0.0f, 1.0f, "%.2f");
                NumericSliderPair("Boundary Thickness", &visuals_.boundaryThickness, 0.5f, 6.0f, "%.2f");
                ImGui::Checkbox("Animate Boundary", &visuals_.boundaryAnimate);
                if (accessibility_.reduceMotion) {
                    visuals_.boundaryAnimate = false;
                }
            }
        }
        PopSectionTint();
    }

    void drawAnalysisSection() {
        PushSectionTint(0);
        if (ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (PrimaryButton("Status", ImVec2(90, 26))) {
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
            if (PrimaryButton("Fields", ImVec2(90, 26))) {
                std::string message;
                runtime_.listFields(message);
                appendLog(message);
            }

            ImGui::InputText("Variable", panel_.summaryVariable, sizeof(panel_.summaryVariable));
            if (PrimaryButton("Summary", ImVec2(120, 26))) {
                std::string message;
                runtime_.summarizeField(panel_.summaryVariable, message);
                appendLog(message);
            }

            ImGui::InputText("Checkpoint", panel_.checkpointLabel, sizeof(panel_.checkpointLabel));
            if (PrimaryButton("Create", ImVec2(90, 26))) {
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
        PushSectionTint(1);
        if (ImGui::CollapsingHeader("Accessibility", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool styleChanged = false;
            bool rebuildFonts = false;

            styleChanged |= NumericSliderPair("UI Scale", &accessibility_.uiScale, 0.75f, 3.0f, "%.2f");
            if (NumericSliderPair("Font Size", &accessibility_.fontSizePx, 10.0f, 32.0f, "%.1f")) {
                styleChanged = true;
                rebuildFonts = true;
            }

            styleChanged |= ImGui::Checkbox("High Contrast", &accessibility_.highContrast);
            styleChanged |= ImGui::Checkbox("Keyboard Navigation", &accessibility_.keyboardNav);
            styleChanged |= ImGui::Checkbox("Focus Indicators", &accessibility_.focusIndicators);
            ImGui::Checkbox("Reduce Motion", &accessibility_.reduceMotion);

            if (styleChanged) {
                applyTheme(rebuildFonts);
            }
        }
        PopSectionTint();
    }

    void drawLogSection() {
        if (ImGui::CollapsingHeader("Event Log", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("log_scroller", ImVec2(0, 180), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const std::string& line : logs_) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
    }

    void syncPanelFromConfig() {
        const auto& config = runtime_.config();
        panel_.seed = config.seed;
        panel_.gridWidth = static_cast<int>(config.grid.width);
        panel_.gridHeight = static_cast<int>(config.grid.height);
        panel_.tierIndex = (config.tier == ModelTier::A) ? 0 : (config.tier == ModelTier::B) ? 1 : 2;

        const std::string temporal = app::temporalPolicyToString(config.temporalPolicy);
        panel_.temporalIndex = (temporal == "uniform") ? 0 : (temporal == "phased") ? 1 : 2;
    }

    void applyConfigFromPanel() {
        app::LaunchConfig config = runtime_.config();
        config.seed = panel_.seed;
        config.grid = GridSpec{static_cast<std::uint32_t>(std::max(1, panel_.gridWidth)), static_cast<std::uint32_t>(std::max(1, panel_.gridHeight))};
        config.tier = (panel_.tierIndex == 0) ? ModelTier::A : (panel_.tierIndex == 1) ? ModelTier::B : ModelTier::C;
        const auto parsedTemporal = app::parseTemporalPolicy(kTemporalOptions[panel_.temporalIndex]);
        if (parsedTemporal.has_value()) {
            config.temporalPolicy = *parsedTemporal;
        }

        runtime_.setConfig(config);

        std::ostringstream output;
        output << "config_applied seed=" << config.seed
               << " grid=" << config.grid.width << 'x' << config.grid.height
               << " tier=" << toString(config.tier)
               << " temporal=" << app::temporalPolicyToString(config.temporalPolicy);
        appendLog(output.str());
    }

    void triggerOverlay(const OverlayIcon icon) {
        overlay_.icon = icon;
        overlay_.alpha = 1.0f;
    }

    void appendLog(const std::string& line) {
        if (!line.empty()) {
            logs_.push_back(line);
        }
        if (logs_.size() > 1000) {
            logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 1000));
        }
    }

    AccessibilityConfig accessibility_{};
    PanelState panel_{};
    VisualParams visuals_{};
    OverlayState overlay_{};
    std::vector<std::string> logs_;
    RuntimeService runtime_;
};

} // namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui
