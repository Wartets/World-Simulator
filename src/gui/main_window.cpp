#include "ws/gui/main_window.hpp"

#include "ws/gui/display_manager.hpp"
#include "ws/gui/main_window/color_utils.hpp"
#include "ws/gui/main_window/detail_utils.hpp"
#include "ws/gui/main_window/panel_state.hpp"
#include "ws/gui/runtime_service.hpp"
#include "ws/gui/session_manager/session_manager.hpp"
#include "ws/gui/theme_bootstrap.hpp"
#include "ws/gui/ui_components.hpp"

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <future>
#include <fstream>
#include <random>

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

    bool showGrid = false;
    float gridOpacity = 0.35f;
    float gridLineThickness = 1.0f;

    bool showBoundary = true;
    float boundaryOpacity = 0.85f;
    float boundaryThickness = 1.2f;
    bool boundaryAnimate = true;
};

using PanelState = main_window::PanelState;

struct OverlayState {
    float alpha = 0.0f;
    OverlayIcon icon = OverlayIcon::None;
};

enum class ScreenLayout {
    Single = 0,
    SplitLeftRight = 1,
    SplitTopBottom = 2,
    Quad = 3
};

enum class NormalizationMode {
    PerFrameAuto = 0,
    StickyPerField = 1,
    FixedManual = 2
};

enum class ColorMapMode {
    Turbo = 0,
    Grayscale = 1,
    Diverging = 2,
    Water = 3
};

enum class AppState {
    SessionManager,
    NewWorldWizard,
    Simulation
};

struct ViewportConfig {
    int primaryFieldIndex = 0;
    DisplayType displayType = DisplayType::ScalarField;
    
    NormalizationMode normalizationMode = NormalizationMode::StickyPerField;
    ColorMapMode colorMapMode = ColorMapMode::Turbo;
    float fixedRangeMin = 0.0f;
    float fixedRangeMax = 1.0f;
    std::unordered_map<std::string, std::pair<float, float>> stickyRanges;

    bool showLegend = true;
    bool showRangeDetails = true;

    bool showVectorField = false;
    int vectorXFieldIndex = 0;
    int vectorYFieldIndex = 1;
    int vectorStride = 6;
    float vectorScale = 0.45f;
};

struct VisualizationState {
    ScreenLayout layout = ScreenLayout::SplitLeftRight;
    std::array<ViewportConfig, 4> viewports = []() {
        std::array<ViewportConfig, 4> arr;
        // Default to distinct views for better out-of-the-box experience
        arr[0].primaryFieldIndex = 0; // Configured normally for Elevation
        arr[0].colorMapMode = ColorMapMode::Turbo;

        arr[1].primaryFieldIndex = 1; // E.g., Water or Humidity
        arr[1].colorMapMode = ColorMapMode::Water;

        arr[2].primaryFieldIndex = 2; // E.g., Temperature
        arr[2].colorMapMode = ColorMapMode::Turbo;

        arr[3].primaryFieldIndex = 3; // E.g., Wind
        arr[3].colorMapMode = ColorMapMode::Turbo;
        return arr;
    }();
    int activeViewportEditor = 0;

    bool showCellGrid = false;
    bool showSparseOverlay = true;
    bool autoRun = true;
    int autoStepsPerFrame = 1;
    int simulationTickHz = 120;
    float snapshotRefreshHz = 120.0f;
    bool adaptiveSampling = true;
    int manualSamplingStride = 1;
    int maxRenderedCells = 220000;

    DisplayType generationPreviewDisplayType = DisplayType::SurfaceCategory;
    DisplayManagerParams displayManager{};

    std::vector<std::string> fieldNames;
    RuntimeCheckpoint cachedCheckpoint{};
    bool hasCachedCheckpoint = false;
    double lastSnapshotTimeSec = -1.0;
    bool snapshotDirty = true;
    float lastSnapshotDurationMs = 0.0f;
    int framesSinceSnapshot = 0;
    char lastRuntimeError[256] = "";
    std::unordered_map<std::string, std::pair<float, float>> stickyRanges;
};

constexpr std::array<const char*, 3> kTierOptions = {"A (Baseline)", "B (Intermediate)", "C (Advanced)"};
constexpr std::array<const char*, 3> kTemporalOptions = {"uniform", "phased", "multirate"};
constexpr int kImGuiIntSafeMax = std::numeric_limits<int>::max() / 2;
constexpr float kS1 = 4.0f;
constexpr float kS2 = 8.0f;
constexpr float kS3 = 12.0f;
constexpr float kS4 = 16.0f;
constexpr float kS5 = 24.0f;
constexpr float kS6 = 32.0f;
constexpr float kPageMaxWidth = 1600.0f;

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

using main_window::detail::applyDisplayTransfer;
using main_window::detail::colormapDiverging;
using main_window::detail::colormapGrayscale;
using main_window::detail::colormapTurboLike;
using main_window::detail::colormapWater;

using main_window::detail::hashCombine;
using main_window::detail::hashFloat;
using main_window::detail::mergedFieldValues;
using main_window::detail::minMaxFinite;
using main_window::detail::previewTerrainValue;
using main_window::detail::unpackColor;

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

        loadDisplayPrefs();

        // std::string startupMessage;
        // runtime_.start(startupMessage);
        // appendLog(startupMessage);

        syncPanelFromConfig();
        refreshFieldNames();
        requestSnapshotRefresh();
        startSnapshotWorker();

        double previousTimeSec = glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            const double nowSec = glfwGetTime();
            const float frameDt = static_cast<float>(nowSec - previousTimeSec);
            previousTimeSec = nowSec;

            glfwPollEvents();

            tickAutoRun(frameDt);
            consumeSnapshotFromWorker();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            uiParameterChangedThisFrame_ = false;
            uiParameterInteractingThisFrame_ = false;

            if (appState_ == AppState::Simulation) {
                drawViewport();
                drawDockSpace();
                drawControlPanel();
                drawSimulationCanvas();
                drawPlaybackOverlay(overlay_, accessibility_.reduceMotion, ImGui::GetIO().DeltaTime);
            } else if (appState_ == AppState::SessionManager) {
                drawSessionManager();
            } else if (appState_ == AppState::NewWorldWizard) {
                drawNewWorldWizard();
            }

            ImGui::Render();

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            glViewport(0, 0, framebufferWidth, framebufferHeight);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        stopSnapshotWorker();
        destroyRasterResources();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }

private:
    struct ViewMapping {
        ImVec2 viewportMin{};
        ImVec2 viewportMax{};
        ImVec2 contentMin{};
        float cellW = 1.0f;
        float cellH = 1.0f;
        int samplingStride = 1;
    };

    struct RasterTexture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    std::future<void> autoRunFuture_;
    std::mutex asyncStateMutex_;
    std::string asyncErrorMessage_;
    bool asyncErrorPending_ = false;

    struct RenderCacheState {
        int snapshotGeneration = -1;
        std::uint64_t configHash = 0;
        bool valid = false;
        float primaryMin = 0.0f;
        float primaryMax = 1.0f;
        std::string primaryName;
    };


#define WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT 1
#include "main_window/impl/runtime_visualization.inl"
#include "main_window/impl/session_wizard.inl"
#include "main_window/impl/controls_and_config.inl"
#undef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    AccessibilityConfig accessibility_{};
    PanelState panel_{};
    session_manager::SessionUiState sessionUi_{};
    VisualParams visuals_{};
    VisualizationState viz_{};
    OverlayState overlay_{};
    std::vector<std::string> logs_;
    AppState appState_ = AppState::SessionManager;

    std::thread snapshotWorker_;
    std::atomic<bool> snapshotWorkerRunning_{false};
    std::atomic<bool> snapshotRequestPending_{true};
    std::atomic<bool> snapshotContinuousMode_{false};
    std::atomic<float> snapshotRefreshHzAtomic_{120.0f};
    std::atomic<float> snapshotDurationMsAtomic_{0.0f};
    std::atomic<int> snapshotFrontIndex_{0};
    std::atomic<int> snapshotGeneration_{0};
    int consumedSnapshotGeneration_ = 0;
    std::array<RuntimeCheckpoint, 2> snapshotBuffers_{};
    std::array<bool, 2> snapshotBufferValid_{false, false};
    std::mutex snapshotBufferMutex_;
    std::mutex snapshotErrorMutex_;
    std::string snapshotWorkerError_;

    std::array<RasterTexture, 4> viewportRasters_{};
    std::array<std::vector<std::uint8_t>, 4> rasterBuffers_{};
    RasterTexture wizardPreviewTexture_{};
    std::vector<std::uint8_t> wizardPreviewPixels_{};
    std::uint64_t wizardPreviewHash_ = 0;
    int wizardPreviewW_ = 0;
    int wizardPreviewH_ = 0;
    int wizardPreviewStride_ = 1;
    float wizardPreviewWaterLevel_ = 0.0f;
    std::array<RenderCacheState, 4> renderCaches_{};
    std::unordered_map<std::uint64_t, DisplayBuffer> snapshotDisplayCache_{};
    int snapshotDisplayCacheGeneration_ = -1;
    int maxTextureSize_ = 0;
    int nextViewportRebuildCursor_ = 0;

    bool uiParameterChangedThisFrame_ = false;
    bool uiParameterInteractingThisFrame_ = false;
    double uiInteractionHotUntilSec_ = 0.0;

    double autoRunStepBudget_ = 0.0;
    RuntimeService runtime_;
};

} // namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui


