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
#include <condition_variable>
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
#include <fstream>
#include <random>

namespace ws::gui {
namespace {

enum class OverlayIcon { None, Play, Pause };

struct VisualParams {
    float zoom        = 1.0f;
    float panX        = 0.0f;
    float panY        = 0.0f;
    float brightness  = 1.0f;
    float contrast    = 1.0f;
    float gamma       = 1.0f;
    bool  invertColors = false;
    bool  showGrid    = false;
    float gridOpacity = 0.35f;
    float gridLineThickness = 1.0f;
    bool  showBoundary      = true;
    float boundaryOpacity   = 0.85f;
    float boundaryThickness = 1.2f;
    bool  boundaryAnimate   = true;
};

using PanelState = main_window::PanelState;

struct OverlayState {
    float alpha     = 0.0f;
    OverlayIcon icon = OverlayIcon::None;
};

enum class ScreenLayout    { Single = 0, SplitLeftRight, SplitTopBottom, Quad };
enum class NormalizationMode { PerFrameAuto = 0, StickyPerField, FixedManual };
enum class ColorMapMode    { Turbo = 0, Grayscale, Diverging, Water };
enum class AppState        { SessionManager, NewWorldWizard, Simulation };

struct ViewportConfig {
    int  primaryFieldIndex = 0;
    DisplayType displayType = DisplayType::ScalarField;

    NormalizationMode normalizationMode = NormalizationMode::StickyPerField;
    ColorMapMode      colorMapMode      = ColorMapMode::Turbo;
    float fixedRangeMin = 0.0f;
    float fixedRangeMax = 1.0f;
    std::unordered_map<std::string, std::pair<float,float>> stickyRanges;

    bool showLegend      = true;
    bool showRangeDetails = true;
    bool showWindMagnitudeBackground = true;

    bool  showVectorField  = false;
    int   vectorXFieldIndex = 0;
    int   vectorYFieldIndex = 1;
    int   vectorStride      = 6;
    float vectorScale       = 0.45f;
};

struct VisualizationState {
    ScreenLayout layout = ScreenLayout::SplitLeftRight;
    std::array<ViewportConfig,4> viewports = [](){
        std::array<ViewportConfig,4> a;
        a[0].primaryFieldIndex = 0; a[0].colorMapMode = ColorMapMode::Turbo;
        a[1].primaryFieldIndex = 1; a[1].colorMapMode = ColorMapMode::Water;
        a[2].primaryFieldIndex = 2; a[2].colorMapMode = ColorMapMode::Turbo;
        a[3].primaryFieldIndex = 3; a[3].colorMapMode = ColorMapMode::Turbo;
        return a;
    }();
    int activeViewportEditor = 0;

    bool  showCellGrid      = false;
    bool  showSparseOverlay = true;
    bool  autoRun           = true;
    int   displayRefreshEveryNSteps = 1;
    bool  unlimitedSimSpeed = true;
    bool  adaptiveSampling   = true;
    int   manualSamplingStride = 1;
    int   maxRenderedCells    = 220000;

    DisplayType       generationPreviewDisplayType = DisplayType::SurfaceCategory;
    DisplayManagerParams displayManager{};

    std::vector<std::string> fieldNames;
    RuntimeCheckpoint cachedCheckpoint{};
    bool  hasCachedCheckpoint  = false;
    double lastSnapshotTimeSec = -1.0;
    bool  snapshotDirty        = true;
    float lastSnapshotDurationMs = 0.0f;
    int   framesSinceSnapshot  = 0;
    char  lastRuntimeError[256] = "";
    std::unordered_map<std::string, std::pair<float,float>> stickyRanges;
};

constexpr std::array<const char*, 3> kTierOptions    = {"A (Baseline)","B (Intermediate)","C (Advanced)"};
constexpr std::array<const char*, 3> kTemporalOptions = {"uniform","phased","multirate"};
constexpr int   kImGuiIntSafeMax = std::numeric_limits<int>::max() / 2;
constexpr float kS2 = 8.0f;
constexpr float kS3 = 12.0f;
constexpr float kS5 = 24.0f;
constexpr float kPageMaxWidth = 1600.0f;

// GLFW window creation
GLFWwindow* createGlfwWindowWithFallback() {
    struct VP { int major, minor; };
    constexpr std::array<VP,3> profiles = {VP{4,5},VP{4,1},VP{3,3}};
    for (const auto& p : profiles) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, p.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, p.minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        if (auto* w = glfwCreateWindow(1600, 960, "World Simulator", nullptr, nullptr)) return w;
    }
    return nullptr;
}

// Playback overlay
void drawPlaybackOverlay(OverlayState& overlay, bool reduceMotion, float dt) {
    if (reduceMotion) { overlay.alpha = 0.0f; return; }
    overlay.alpha = std::max(0.0f, overlay.alpha - 1.2f * dt);
    if (overlay.alpha <= 0.0f || overlay.icon == OverlayIcon::None) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 center(ImGui::GetIO().DisplaySize.x - 44.0f, 44.0f);
    const float r = 22.0f;
    const int   a = static_cast<int>(overlay.alpha * 255.0f);
    dl->AddCircleFilled(center, r, IM_COL32(35,45,70,a));
    dl->AddCircle(center, r, IM_COL32(130,170,255,a), 24, 2.0f);
    if (overlay.icon == OverlayIcon::Play) {
        dl->AddTriangleFilled({center.x-6,center.y-8},{center.x-6,center.y+8},
                               {center.x+9,center.y}, IM_COL32(240,245,255,a));
    } else {
        dl->AddRectFilled({center.x-8,center.y-8},{center.x-2,center.y+8},IM_COL32(240,245,255,a));
        dl->AddRectFilled({center.x+2,center.y-8},{center.x+8,center.y+8},IM_COL32(240,245,255,a));
    }
}

// Aliases from detail_utils / color_utils
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

//
class MainWindowImpl {
public:
    int run() {
        if (glfwInit() != GLFW_TRUE) return 1;
        GLFWwindow* window = createGlfwWindowWithFallback();
        if (!window) { glfwTerminate(); return 1; }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // default on

        ThemeBootstrap::applyBaseTheme(ImGui::GetStyle(), accessibility_.uiScale);
        ThemeBootstrap::configureFont(io, accessibility_.fontSizePx);
        ThemeBootstrap::applyAccessibility(io, ImGui::GetStyle(), accessibility_);

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        syncPanelFromConfig();
        refreshFieldNames();
        requestSnapshotRefresh();
        startSnapshotWorker();
        startSimulationWorker();

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            tickAutoRun();
            consumeSnapshotFromWorker();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            uiParameterChangedThisFrame_    = false;
            uiParameterInteractingThisFrame_ = false;

            if (appState_ == AppState::Simulation) {
                drawViewport();
                drawDockSpace();
                drawControlPanel();
                drawSimulationCanvas();
                drawPlaybackOverlay(overlay_, accessibility_.reduceMotion,
                                    ImGui::GetIO().DeltaTime);
            } else if (appState_ == AppState::SessionManager) {
                drawSessionManager();
            } else if (appState_ == AppState::NewWorldWizard) {
                drawNewWorldWizard();
            }

            ImGui::Render();
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            glViewport(0, 0, fbW, fbH);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        stopSimulationWorker();
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
    // per-viewport raster
    struct ViewMapping {
        ImVec2 viewportMin{}, viewportMax{}, contentMin{};
        float cellW = 1.0f, cellH = 1.0f;
        int samplingStride = 1;
    };
    struct RasterTexture { GLuint id = 0; int width = 0, height = 0; };
    struct RenderCacheState {
        int snapshotGeneration = -1;
        std::uint64_t configHash = 0;
        bool valid = false;
        float primaryMin = 0.0f, primaryMax = 1.0f;
        std::string primaryName;
    };

    // persistent auto-run worker
    std::thread simulationThread_;
    std::mutex simulationWakeMutex_;
    std::condition_variable simulationWakeCV_;
    std::atomic<bool> simulationThreadRunning_{false};
    std::atomic<bool> simulationAutoRunEnabled_{false};
    std::atomic<bool> simulationWorkerBusy_{false};
    std::atomic<int>  simulationDisplayRefreshEveryNSteps_{1};
    std::atomic<bool> simulationUnlimitedSpeed_{true};
    std::atomic<std::uint64_t> simulationLastDisplayRequestMs_{0};
    std::atomic<float> simulationLastBatchDurationMs_{0.0f};
    std::atomic<int>   simulationLastBatchSteps_{0};
    std::mutex asyncStateMutex_;
    std::string asyncErrorMessage_;
    bool asyncErrorPending_ = false;

// inlined implementation files
#define WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT 1
#include "main_window/impl/runtime_visualization.inl"
#include "main_window/impl/session_wizard.inl"
#include "main_window/impl/controls_and_config.inl"
#undef WS_MAIN_WINDOW_IMPL_CLASS_CONTEXT

    // member state
    AccessibilityConfig  accessibility_{};
    PanelState           panel_{};
    session_manager::SessionUiState sessionUi_{};
    VisualParams         visuals_{};
    VisualizationState   viz_{};
    OverlayState         overlay_{};
    std::vector<std::string> logs_;
    AppState             appState_ = AppState::SessionManager;

    // snapshot double-buffer
    std::thread       snapshotWorker_;
    std::mutex        snapshotWakeMutex_;
    std::condition_variable snapshotWakeCV_;
    std::atomic<bool> snapshotWorkerRunning_{false};
    std::atomic<bool> snapshotRequestPending_{true};
    std::atomic<float> snapshotDurationMsAtomic_{0.0f};
    std::atomic<int>   snapshotFrontIndex_{0};
    std::atomic<int>   snapshotGeneration_{0};
    int consumedSnapshotGeneration_ = 0;
    std::array<RuntimeCheckpoint,2> snapshotBuffers_{};
    std::array<bool,2> snapshotBufferValid_{false,false};
    std::mutex snapshotBufferMutex_;
    std::mutex snapshotErrorMutex_;
    std::string snapshotWorkerError_;

    // raster textures
    std::array<RasterTexture,4>            viewportRasters_{};
    std::array<std::vector<std::uint8_t>,4> rasterBuffers_{};
    RasterTexture         wizardPreviewTexture_{};
    std::vector<std::uint8_t> wizardPreviewPixels_{};
    std::uint64_t         wizardPreviewHash_     = 0;
    int                   wizardPreviewW_         = 0;
    int                   wizardPreviewH_         = 0;
    int                   wizardPreviewStride_    = 1;
    float                 wizardPreviewWaterLevel_ = 0.0f;
    std::array<RenderCacheState,4> renderCaches_{};
    std::unordered_map<std::uint64_t, DisplayBuffer> snapshotDisplayCache_;
    int snapshotDisplayCacheGeneration_ = -1;
    int maxTextureSize_ = 0;
    int nextViewportRebuildCursor_ = 0;

    // UI interaction state
    bool   uiParameterChangedThisFrame_    = false;
    bool   uiParameterInteractingThisFrame_ = false;
    double uiInteractionHotUntilSec_       = 0.0;

    RuntimeService runtime_;
};

} // anonymous namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui
