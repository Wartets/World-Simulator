#include "ws/gui/main_window.hpp"

#include "ws/gui/display_manager.hpp"
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

struct PanelState {
    std::uint64_t seed = 42;
    bool useManualSeed = false;
    int gridWidth = 128;
    int gridHeight = 128;
    int tierIndex = 0;
    int temporalIndex = 0;

    int stepCount = 1;
    std::uint64_t runUntilTarget = 100;
    bool showAdvancedStepping = false;

    float forceFieldScale = 1.0f;
    float forceFieldDamping = 0.15f;
    float particleMobility = 0.6f;
    float particleCohesion = 0.2f;
    float constraintRigidity = 0.5f;
    float constraintTolerance = 0.1f;

    float terrainBaseFrequency = 2.2f;
    float terrainDetailFrequency = 7.5f;
    float terrainWarpStrength = 0.55f;
    float terrainAmplitude = 1.0f;
    float terrainRidgeMix = 0.28f;
    int terrainOctaves = 5;
    float terrainLacunarity = 2.0f;
    float terrainGain = 0.5f;
    float seaLevel = 0.48f;
    float polarCooling = 0.62f;
    float latitudeBanding = 1.0f;
    float humidityFromWater = 0.52f;
    float biomeNoiseStrength = 0.20f;
    float islandDensity = 0.58f;
    float islandFalloff = 1.35f;
    float coastlineSharpness = 1.10f;
    float archipelagoJitter = 0.85f;
    float erosionStrength = 0.32f;
    float shelfDepth = 0.20f;

    char profileName[128] = "baseline";
    char summaryVariable[128] = "temperature_T";
    char checkpointLabel[128] = "quick";
};

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
    int simulationTickHz = 30;
    float snapshotRefreshHz = 12.0f;
    bool adaptiveSampling = true;
    int manualSamplingStride = 1;
    int maxRenderedCells = 180000;

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

ImU32 colormapTurboLike(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const float r = std::clamp(1.5f * t, 0.0f, 1.0f);
    const float g = std::clamp(1.5f * (1.0f - std::abs(2.0f * t - 1.0f)), 0.0f, 1.0f);
    const float b = std::clamp(1.5f * (1.0f - t), 0.0f, 1.0f);
    return IM_COL32(
        static_cast<int>(r * 255.0f),
        static_cast<int>(g * 255.0f),
        static_cast<int>(b * 255.0f),
        255);
}

ImU32 colormapGrayscale(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const int c = static_cast<int>(t * 255.0f);
    return IM_COL32(c, c, c, 255);
}

ImU32 colormapDiverging(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const float k = (t - 0.5f) * 2.0f;
    if (k >= 0.0f) {
        const int r = static_cast<int>((0.4f + 0.6f * k) * 255.0f);
        const int g = static_cast<int>((0.2f + 0.5f * (1.0f - k)) * 255.0f);
        const int b = static_cast<int>((0.2f + 0.4f * (1.0f - k)) * 255.0f);
        return IM_COL32(r, g, b, 255);
    }

    const float a = -k;
    const int r = static_cast<int>((0.2f + 0.4f * (1.0f - a)) * 255.0f);
    const int g = static_cast<int>((0.3f + 0.5f * (1.0f - a)) * 255.0f);
    const int b = static_cast<int>((0.45f + 0.55f * a) * 255.0f);
    return IM_COL32(r, g, b, 255);
}

ImU32 colormapWater(const float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    const int r = static_cast<int>(t * 120.0f);
    const int g = static_cast<int>(t * t * 180.0f + 70.0f);
    const int b = static_cast<int>(t * 60.0f + 195.0f);
    return IM_COL32(r, g, b, 255);
}

float applyDisplayTransfer(float t, const VisualParams& visuals) {
    float v = std::clamp(t, 0.0f, 1.0f);
    v = (v - 0.5f) * visuals.contrast + 0.5f;
    v *= visuals.brightness;
    v = std::clamp(v, 0.0f, 1.0f);

    const float gamma = std::max(0.05f, visuals.gamma);
    v = std::pow(v, 1.0f / gamma);

    if (visuals.invertColors) {
        v = 1.0f - v;
    }
    return std::clamp(v, 0.0f, 1.0f);
}

std::vector<float> mergedFieldValues(const StateStoreSnapshot::FieldPayload& field, const bool includeSparseOverlay) {
    const std::size_t n = field.values.size();
    std::vector<float> merged(n, std::numeric_limits<float>::quiet_NaN());

    for (std::size_t i = 0; i < n; ++i) {
        if (i < field.validityMask.size() && field.validityMask[i] != 0u) {
            merged[i] = field.values[i];
        }
    }

    if (includeSparseOverlay) {
        for (const auto& [idx, value] : field.sparseOverlay) {
            if (idx < merged.size()) {
                merged[static_cast<std::size_t>(idx)] = value;
            }
        }
    }

    return merged;
}

void minMaxFinite(const std::vector<float>& values, float& outMin, float& outMax) {
    outMin = std::numeric_limits<float>::infinity();
    outMax = -std::numeric_limits<float>::infinity();
    for (const float value : values) {
        if (std::isfinite(value)) {
            outMin = std::min(outMin, value);
            outMax = std::max(outMax, value);
        }
    }
    if (!std::isfinite(outMin) || !std::isfinite(outMax)) {
        outMin = 0.0f;
        outMax = 1.0f;
    }
    if (std::abs(outMax - outMin) < 1e-12f) {
        outMax = outMin + 1.0f;
    }
}

std::uint64_t hashCombine(std::uint64_t seed, const std::uint64_t value) {
    constexpr std::uint64_t k = 0x9e3779b97f4a7c15ull;
    return seed ^ (value + k + (seed << 6u) + (seed >> 2u));
}

std::uint64_t hashFloat(const float value) {
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

void unpackColor(const ImU32 color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) {
    r = static_cast<std::uint8_t>((color) & 0xFFu);
    g = static_cast<std::uint8_t>((color >> 8u) & 0xFFu);
    b = static_cast<std::uint8_t>((color >> 16u) & 0xFFu);
    a = static_cast<std::uint8_t>((color >> 24u) & 0xFFu);
}

float hashNoise(const std::uint64_t seed, const int x, const int y) {
    std::uint64_t h = seed;
    h = hashCombine(h, static_cast<std::uint64_t>(x * 73856093));
    h = hashCombine(h, static_cast<std::uint64_t>(y * 19349663));
    h ^= (h >> 33u);
    h *= 0xff51afd7ed558ccdull;
    h ^= (h >> 33u);
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= (h >> 33u);
    const double normalized = static_cast<double>(h & 0xFFFFFFFFull) / static_cast<double>(0xFFFFFFFFull);
    return static_cast<float>(normalized);
}

struct PreviewZoneSample {
    float zoneValue = 0.5f;
    float edgeBlend = 0.0f;
};

struct PreviewIslandSample {
    float landMask = 0.0f;
    float shelfMask = 0.0f;
};

PreviewZoneSample previewZoneSample(const std::uint64_t seed, const float x, const float y, const float zoneScale) {
    const float px = x / std::max(0.001f, zoneScale);
    const float py = y / std::max(0.001f, zoneScale);
    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));

    float bestDist2 = std::numeric_limits<float>::infinity();
    float secondDist2 = std::numeric_limits<float>::infinity();
    float bestValue = 0.5f;

    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int sx = cx + ox;
            const int sy = cy + oy;
            const float jitterX = hashNoise(seed ^ 0x11ull, sx, sy) - 0.5f;
            const float jitterY = hashNoise(seed ^ 0x37ull, sx, sy) - 0.5f;
            const float siteX = static_cast<float>(sx) + 0.5f + 0.8f * jitterX;
            const float siteY = static_cast<float>(sy) + 0.5f + 0.8f * jitterY;
            const float dx = px - siteX;
            const float dy = py - siteY;
            const float dist2 = dx * dx + dy * dy;

            if (dist2 < bestDist2) {
                secondDist2 = bestDist2;
                bestDist2 = dist2;
                bestValue = hashNoise(seed ^ 0x5Bull, sx, sy);
            } else if (dist2 < secondDist2) {
                secondDist2 = dist2;
            }
        }
    }

    const float edge = std::clamp((std::sqrt(secondDist2) - std::sqrt(bestDist2)) * 0.8f, 0.0f, 1.0f);
    return PreviewZoneSample{bestValue, edge};
}

PreviewIslandSample previewIslandMask(
    const std::uint64_t seed,
    const float nx,
    const float ny,
    const float density,
    const float jitter,
    const float falloff) {
    const float cellScale = std::clamp(0.22f - 0.12f * density, 0.07f, 0.24f);
    const float px = nx / std::max(0.001f, cellScale);
    const float py = ny / std::max(0.001f, cellScale);
    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));

    float best = 0.0f;
    float shelf = 0.0f;
    const float spawnThreshold = std::clamp(0.28f + 0.52f * density, 0.15f, 0.92f);

    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int sx = cx + ox;
            const int sy = cy + oy;
            const float spawn = hashNoise(seed ^ 0xA9ull, sx, sy);
            if (spawn > spawnThreshold) {
                continue;
            }

            const float jx = (hashNoise(seed ^ 0xB4ull, sx, sy) - 0.5f) * jitter;
            const float jy = (hashNoise(seed ^ 0xC8ull, sx, sy) - 0.5f) * jitter;
            const float siteX = static_cast<float>(sx) + 0.5f + jx;
            const float siteY = static_cast<float>(sy) + 0.5f + jy;
            const float radius = std::max(0.18f, 0.38f + 1.20f * hashNoise(seed ^ 0xD1ull, sx, sy));

            const float dx = px - siteX;
            const float dy = py - siteY;
            const float dNorm = std::sqrt(dx * dx + dy * dy) / radius;
            const float core = std::clamp(1.0f - dNorm, 0.0f, 1.0f);
            best = std::max(best, std::pow(core, std::max(0.5f, falloff)));
            shelf = std::max(shelf, std::pow(std::clamp(1.0f - dNorm * 0.72f, 0.0f, 1.0f), 1.7f));
        }
    }

    return PreviewIslandSample{best, shelf};
}

float previewTerrainValue(const PanelState& panel, const int x, const int y, const int w, const int h) {
    const float nx = static_cast<float>(x) / std::max(1, w - 1);
    const float ny = static_cast<float>(y) / std::max(1, h - 1);

    float freq = std::max(0.01f, panel.terrainBaseFrequency);
    float amplitude = std::max(0.01f, panel.terrainAmplitude);
    float value = 0.0f;
    float ampAccum = 0.0f;
    const int octaves = std::clamp(panel.terrainOctaves, 1, 8);

    const float warpX = hashNoise(panel.seed ^ 0xABull, x / 5, y / 5) - 0.5f;
    const float warpY = hashNoise(panel.seed ^ 0xCDull, x / 5, y / 5) - 0.5f;
    const float zoneScale = std::max(0.08f, 0.40f / std::max(0.25f, panel.terrainBaseFrequency));
    const float domainX = nx + warpX * panel.terrainWarpStrength;
    const float domainY = ny + warpY * panel.terrainWarpStrength;
    const PreviewZoneSample zone = previewZoneSample(panel.seed ^ 0xEFull, domainX, domainY, zoneScale);
    const PreviewIslandSample islands = previewIslandMask(
        panel.seed ^ 0x913ull,
        domainX,
        domainY,
        std::clamp(panel.islandDensity, 0.05f, 0.95f),
        std::clamp(panel.archipelagoJitter, 0.0f, 1.5f),
        std::clamp(panel.islandFalloff, 0.35f, 4.5f));
    const float zoneBias = zone.zoneValue - 0.5f;

    for (int i = 0; i < octaves; ++i) {
        const float sampleX = (nx + warpX * panel.terrainWarpStrength) * freq;
        const float sampleY = (ny + warpY * panel.terrainWarpStrength) * freq;
        const float wave = 0.5f + 0.5f * std::sin(sampleX * 6.28318f + 0.5f * std::cos(sampleY * 6.28318f));
        const float noise = hashNoise(panel.seed + static_cast<std::uint64_t>(i * 7919), x + (i * 13), y + (i * 17));
        const float ridge = 1.0f - std::abs(2.0f * noise - 1.0f);
        const float mixed = (wave * (1.0f - panel.terrainRidgeMix)) + (ridge * (panel.terrainRidgeMix + 0.20f * std::clamp(zoneBias, 0.0f, 1.0f)));
        value += mixed * amplitude;
        ampAccum += amplitude;
        freq *= std::max(1.0f, panel.terrainLacunarity);
        amplitude *= std::clamp(panel.terrainGain, 0.1f, 1.0f);
    }

    if (ampAccum > 0.0f) {
        value /= ampAccum;
    }

    const float latitude = std::abs((ny - 0.5f) * 2.0f);
    const float islandLift = 0.56f * islands.landMask + 0.12f * islands.shelfMask - 0.20f * (1.0f - islands.landMask);
    const float erosion = (hashNoise(panel.seed ^ 0x6A5ull, x, y) - 0.5f) * panel.erosionStrength * 0.35f;
    value = value * 0.58f + islandLift;
    value -= latitude * panel.polarCooling * 0.25f;
    value += 0.10f * zoneBias;
    value += 0.08f * (zone.edgeBlend - 0.5f);
    value += erosion;
    value += (hashNoise(panel.seed ^ 0xA53ull, x, y) - 0.5f) * panel.biomeNoiseStrength;
    return std::clamp(value, 0.0f, 1.0f);
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

            if (appState_ == AppState::Simulation) {
                drawViewport();
                drawSimulationCanvas();
                drawDockSpace();
                drawControlPanel();
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

    void destroyRasterTexture(RasterTexture& texture) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }
        texture.width = 0;
        texture.height = 0;
    }

    void destroyRasterResources() {
        for (auto& raster : viewportRasters_) {
            destroyRasterTexture(raster);
        }
        for (auto& cache : renderCaches_) {
            cache.valid = false;
        }
    }

    void uploadRasterTexture(RasterTexture& texture, const int width, const int height, const std::vector<std::uint8_t>& rgba) {
        if (width <= 0 || height <= 0 || rgba.empty()) {
            destroyRasterTexture(texture);
            return;
        }

        if (texture.id == 0) {
            glGenTextures(1, &texture.id);
        }
        texture.width = width;
        texture.height = height;

        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    [[nodiscard]] std::uint64_t makeRenderConfigHash(const ViewportConfig& vp) const {
        std::uint64_t h = 1469598103934665603ull;
        h = hashCombine(h, static_cast<std::uint64_t>(vp.primaryFieldIndex));
        h = hashCombine(h, static_cast<std::uint64_t>(vp.displayType));
        h = hashCombine(h, static_cast<std::uint64_t>(vp.normalizationMode));
        h = hashCombine(h, static_cast<std::uint64_t>(vp.colorMapMode));
        h = hashCombine(h, static_cast<std::uint64_t>(viz_.showSparseOverlay));
        h = hashCombine(h, hashFloat(vp.fixedRangeMin));
        h = hashCombine(h, hashFloat(vp.fixedRangeMax));
        h = hashCombine(h, static_cast<std::uint64_t>(viz_.displayManager.autoWaterLevel));
        h = hashCombine(h, hashFloat(viz_.displayManager.waterLevel));
        h = hashCombine(h, hashFloat(viz_.displayManager.autoWaterQuantile));
        h = hashCombine(h, hashFloat(viz_.displayManager.lowlandThreshold));
        h = hashCombine(h, hashFloat(viz_.displayManager.highlandThreshold));
        h = hashCombine(h, hashFloat(viz_.displayManager.waterPresenceThreshold));
        h = hashCombine(h, hashFloat(visuals_.brightness));
        h = hashCombine(h, hashFloat(visuals_.contrast));
        h = hashCombine(h, hashFloat(visuals_.gamma));
        h = hashCombine(h, static_cast<std::uint64_t>(visuals_.invertColors));
        return h;
    }

    [[nodiscard]] ViewMapping makeViewMapping(const ImVec2 min, const ImVec2 max, const GridSpec grid) const {
        ViewMapping mapping;

        const float margin = 4.0f;
        const ImVec2 boundedMin(min.x + margin, min.y + margin);
        const ImVec2 boundedMax(max.x - margin, max.y - margin);
        const float boundedW = std::max(1.0f, boundedMax.x - boundedMin.x);
        const float boundedH = std::max(1.0f, boundedMax.y - boundedMin.y);
        const float targetAspect = static_cast<float>(grid.width) / static_cast<float>(grid.height);

        float viewW = boundedW;
        float viewH = boundedW / targetAspect;
        if (viewH > boundedH) {
            viewH = boundedH;
            viewW = boundedH * targetAspect;
        }

        const ImVec2 viewCenter((boundedMin.x + boundedMax.x) * 0.5f, (boundedMin.y + boundedMax.y) * 0.5f);
        mapping.viewportMin = ImVec2(viewCenter.x - viewW * 0.5f, viewCenter.y - viewH * 0.5f);
        mapping.viewportMax = ImVec2(viewCenter.x + viewW * 0.5f, viewCenter.y + viewH * 0.5f);

        const float zoom = std::max(0.05f, visuals_.zoom);
        const float contentW = viewW * zoom;
        const float contentH = viewH * zoom;
        mapping.contentMin = ImVec2(
            viewCenter.x - contentW * 0.5f + visuals_.panX,
            viewCenter.y - contentH * 0.5f + visuals_.panY);

        mapping.cellW = contentW / static_cast<float>(grid.width);
        mapping.cellH = contentH / static_cast<float>(grid.height);

        int stride = std::max(1, viz_.manualSamplingStride);
        if (viz_.adaptiveSampling) {
            const int pixelStrideX = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, mapping.cellW))));
            const int pixelStrideY = static_cast<int>(std::ceil(std::max(1.0f, 1.0f / std::max(0.0001f, mapping.cellH))));
            stride = std::max(stride, std::max(pixelStrideX, pixelStrideY));
        }

        while ((static_cast<int>(grid.width) / stride) * (static_cast<int>(grid.height) / stride) > std::max(1000, viz_.maxRenderedCells)) {
            ++stride;
        }
        mapping.samplingStride = std::max(1, stride);

        return mapping;
    }

    [[nodiscard]] std::pair<float, float> resolveDisplayRange(
        ViewportConfig& vp,
        const std::string& fieldName,
        const float localMin,
        const float localMax) {
        if (vp.normalizationMode == NormalizationMode::FixedManual) {
            const float lo = std::min(vp.fixedRangeMin, vp.fixedRangeMax);
            const float hi = std::max(vp.fixedRangeMin, vp.fixedRangeMax);
            return {lo, std::max(lo + 1e-6f, hi)};
        }

        if (vp.normalizationMode == NormalizationMode::PerFrameAuto) {
            return {localMin, localMax};
        }

        auto it = vp.stickyRanges.find(fieldName);
        if (it == vp.stickyRanges.end()) {
            vp.stickyRanges.emplace(fieldName, std::make_pair(localMin, localMax));
            return {localMin, localMax};
        }

        auto& range = it->second;
        range.first = std::min(range.first, localMin);
        range.second = std::max(range.second, localMax);
        if (std::abs(range.second - range.first) < 1e-6f) {
            range.second = range.first + 1.0f;
        }
        return range;
    }

    [[nodiscard]] ImU32 mapColor(const float normalizedValue, const ColorMapMode mode) const {
        const float t = applyDisplayTransfer(normalizedValue, visuals_);
        switch (mode) {
            case ColorMapMode::Grayscale: return colormapGrayscale(t);
            case ColorMapMode::Diverging: return colormapDiverging(t);
            case ColorMapMode::Water:     return colormapWater(t);
            case ColorMapMode::Turbo:
            default:                      return colormapTurboLike(t);
        }
    }

    [[nodiscard]] ImU32 mapDisplayTypeColor(const float value, const DisplayType type, const ColorMapMode scalarPalette) const {
        if (type == DisplayType::ScalarField) {
            return mapColor(value, scalarPalette);
        }

        if (type == DisplayType::SurfaceCategory) {
            const int cls = static_cast<int>(std::round(value));
            switch (cls) {
                case 0: return IM_COL32(32, 84, 186, 255);   // water
                case 1: return IM_COL32(206, 186, 128, 255); // beach/lowland
                case 2: return IM_COL32(92, 162, 84, 255);   // inland
                case 3: return IM_COL32(126, 136, 110, 255); // upland
                default: return IM_COL32(236, 236, 236, 255); // mountain
            }
        }

        if (type == DisplayType::RelativeElevation) {
            const int cls = static_cast<int>(std::round(value));
            switch (cls) {
                case 0: return IM_COL32(28, 58, 135, 255);
                case 1: return IM_COL32(46, 88, 180, 255);
                case 2: return IM_COL32(73, 125, 196, 255);
                case 3: return IM_COL32(112, 170, 104, 255);
                case 4: return IM_COL32(162, 140, 98, 255);
                default: return IM_COL32(242, 242, 242, 255);
            }
        }

        const float depth = std::clamp(value, 0.0f, 1.0f);
        if (depth <= 0.001f) {
            return IM_COL32(0, 0, 0, 255);
        }
        return colormapWater(applyDisplayTransfer(depth, visuals_));
    }

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

    void drawSimulationCanvas() {
        if (!viz_.hasCachedCheckpoint) {
            return;
        }

        const auto& snapshot = viz_.cachedCheckpoint.stateSnapshot;
        if (snapshot.grid.width == 0 || snapshot.grid.height == 0 || snapshot.fields.empty()) {
            return;
        }

        clampVisualizationIndices();

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 canvasMin(0.0f, 0.0f);
        const ImVec2 canvasMax(display.x, display.y);
        if (canvasMax.x <= canvasMin.x + 8.0f || canvasMax.y <= canvasMin.y + 8.0f) {
            return;
        }

        dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(8, 10, 18, 255), 6.0f);
        const auto& fields = snapshot.fields;

        int numViewports = 1;
        if (viz_.layout == ScreenLayout::SplitLeftRight || viz_.layout == ScreenLayout::SplitTopBottom) {
            numViewports = 2;
        } else if (viz_.layout == ScreenLayout::Quad) {
            numViewports = 4;
        }

        for (int i = 0; i < numViewports; ++i) {
            auto& vp = viz_.viewports[i];
            
            const std::uint64_t renderHash = makeRenderConfigHash(vp);
            auto& cache = renderCaches_[i];
            
            const bool cacheDirty = !cache.valid || cache.snapshotGeneration != consumedSnapshotGeneration_ || cache.configHash != renderHash;
            
            if (cacheDirty) {
                DisplayBuffer display = buildDisplayBufferFromSnapshot(
                    snapshot,
                    vp.primaryFieldIndex,
                    vp.displayType,
                    viz_.showSparseOverlay,
                    viz_.displayManager);
                float pMin = display.minValue;
                float pMax = display.maxValue;
                if (vp.displayType == DisplayType::ScalarField) {
                    const auto pRange = resolveDisplayRange(vp, display.label, pMin, pMax);
                    pMin = pRange.first;
                    pMax = pRange.second;
                }
                
                rebuildRasterTexture(i, snapshot.grid, display.values, pMin, pMax, vp);
                
                cache.snapshotGeneration = consumedSnapshotGeneration_;
                cache.configHash = renderHash;
                cache.valid = true;
                cache.primaryMin = pMin;
                cache.primaryMax = pMax;
                cache.primaryName = display.label;
            }
        }

        // Layout evaluation
        const float midX = (canvasMin.x + canvasMax.x) * 0.5f;
        const float midY = (canvasMin.y + canvasMax.y) * 0.5f;

        auto drawPanel = [&](const int i, const ImVec2 pMin, const ImVec2 pMax) {
            const auto& cache = renderCaches_[i];
            auto& vp = viz_.viewports[i];
            drawRasterPanel(*dl, pMin, pMax, snapshot.grid, cache.primaryName, viewportRasters_[i], cache.primaryMin, cache.primaryMax, vp);
            if (vp.showVectorField) {
                drawVectorOverlay(*dl, pMin, pMax, snapshot.grid, vp);
            }
        };

        if (viz_.layout == ScreenLayout::Single) {
            drawPanel(0, canvasMin, canvasMax);
        } else if (viz_.layout == ScreenLayout::SplitLeftRight) {
            drawPanel(0, canvasMin, ImVec2(midX - 3.0f, canvasMax.y));
            drawPanel(1, ImVec2(midX + 3.0f, canvasMin.y), canvasMax);
        } else if (viz_.layout == ScreenLayout::SplitTopBottom) {
            drawPanel(0, canvasMin, ImVec2(canvasMax.x, midY - 3.0f));
            drawPanel(1, ImVec2(canvasMin.x, midY + 3.0f), canvasMax);
        } else if (viz_.layout == ScreenLayout::Quad) {
            drawPanel(0, canvasMin, ImVec2(midX - 2.0f, midY - 2.0f));
            drawPanel(1, ImVec2(midX + 2.0f, canvasMin.y), ImVec2(canvasMax.x, midY - 2.0f));
            drawPanel(2, ImVec2(canvasMin.x, midY + 2.0f), ImVec2(midX - 2.0f, canvasMax.y));
            drawPanel(3, ImVec2(midX + 2.0f, midY + 2.0f), canvasMax);
        }
    }

    void rebuildRasterTexture(
        const int viewportIndex,
        const GridSpec grid,
        const std::vector<float>& primary,
        const float pMin,
        const float pMax,
        const ViewportConfig& vp) {
        
        auto& raster = viewportRasters_[viewportIndex];
        auto& buffer = rasterBuffers_[viewportIndex];

        if (grid.width == 0 || grid.height == 0) {
            destroyRasterTexture(raster);
            return;
        }

        const int width = static_cast<int>(grid.width);
        const int height = static_cast<int>(grid.height);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        if (maxTextureSize_ <= 0) {
            GLint maxSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
            maxTextureSize_ = std::max(256, static_cast<int>(maxSize));
        }

        const int safeCellBudget = std::max(4096, viz_.maxRenderedCells);
        int sampleStride = 1;
        if (pixelCount > static_cast<std::size_t>(safeCellBudget)) {
            const double ratio = std::sqrt(static_cast<double>(pixelCount) / static_cast<double>(safeCellBudget));
            sampleStride = std::max(1, static_cast<int>(std::ceil(ratio)));
        }

        int sampledWidth = std::max(1, static_cast<int>((grid.width + static_cast<std::uint32_t>(sampleStride) - 1u) / static_cast<std::uint32_t>(sampleStride)));
        int sampledHeight = std::max(1, static_cast<int>((grid.height + static_cast<std::uint32_t>(sampleStride) - 1u) / static_cast<std::uint32_t>(sampleStride)));

        const float aspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
        if (sampledWidth > maxTextureSize_ || sampledHeight > maxTextureSize_) {
            const float widthScale = static_cast<float>(sampledWidth) / static_cast<float>(maxTextureSize_);
            const float heightScale = static_cast<float>(sampledHeight) / static_cast<float>(maxTextureSize_);
            const float scale = std::max(widthScale, heightScale);
            sampledWidth = std::max(1, static_cast<int>(std::floor(static_cast<float>(sampledWidth) / scale)));
            sampledHeight = std::max(1, static_cast<int>(std::floor(static_cast<float>(sampledHeight) / scale)));

            if (aspect >= 1.0f) {
                sampledHeight = std::max(1, static_cast<int>(std::round(static_cast<float>(sampledWidth) / aspect)));
            } else {
                sampledWidth = std::max(1, static_cast<int>(std::round(static_cast<float>(sampledHeight) * aspect)));
            }
        }
        
        const std::size_t sampledPixelCount = static_cast<std::size_t>(sampledWidth) * static_cast<std::size_t>(sampledHeight);
        buffer.assign(sampledPixelCount * 4u, 0u);
        for (int sy = 0; sy < sampledHeight; ++sy) {
            const int gy = std::clamp(
                static_cast<int>((static_cast<double>(sy) / static_cast<double>(std::max(1, sampledHeight - 1))) * static_cast<double>(std::max(0, height - 1))),
                0,
                std::max(0, height - 1));
            for (int sx = 0; sx < sampledWidth; ++sx) {
                const int gx = std::clamp(
                    static_cast<int>((static_cast<double>(sx) / static_cast<double>(std::max(1, sampledWidth - 1))) * static_cast<double>(std::max(0, width - 1))),
                    0,
                    std::max(0, width - 1));

                const std::size_t srcIdx = static_cast<std::size_t>(gy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(gx);
                const float value = srcIdx < primary.size() ? primary[srcIdx] : std::numeric_limits<float>::quiet_NaN();
                ImU32 color = IM_COL32(18, 18, 28, 255);
                if (std::isfinite(value)) {
                    const float t = std::clamp((value - pMin) / (std::max(0.0001f, pMax - pMin)), 0.0f, 1.0f);
                    color = mapDisplayTypeColor(
                        (vp.displayType == DisplayType::ScalarField || vp.displayType == DisplayType::WaterDepth) ? t : value,
                        vp.displayType,
                        vp.colorMapMode);
                }
                std::uint8_t r = 0, g = 0, b = 0, a = 0;
                unpackColor(color, r, g, b, a);
                const std::size_t dstIdx = static_cast<std::size_t>(sy) * static_cast<std::size_t>(sampledWidth) + static_cast<std::size_t>(sx);
                const std::size_t o = dstIdx * 4u;
                buffer[o + 0] = r;
                buffer[o + 1] = g;
                buffer[o + 2] = b;
                buffer[o + 3] = a;
            }
        }
        uploadRasterTexture(raster, sampledWidth, sampledHeight, buffer);
    }

    void drawRasterPanel(
        ImDrawList& dl,
        const ImVec2 min,
        const ImVec2 max,
        const GridSpec grid,
        const std::string& title,
        const RasterTexture& texture,
        const float minV,
        const float maxV,
        const ViewportConfig& vp) const {
        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const float width = mapping.viewportMax.x - mapping.viewportMin.x;
        const float height = mapping.viewportMax.y - mapping.viewportMin.y;
        if (width <= 4.0f || height <= 4.0f || grid.width == 0 || grid.height == 0) {
            return;
        }

        dl.AddRectFilled(mapping.viewportMin, mapping.viewportMax, IM_COL32(12, 14, 22, 255), 2.0f);
        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        if (texture.id != 0) {
            const ImVec2 contentMax(
                mapping.contentMin.x + mapping.cellW * static_cast<float>(grid.width),
                mapping.contentMin.y + mapping.cellH * static_cast<float>(grid.height));
            dl.AddImage(
                static_cast<ImTextureID>(texture.id),
                mapping.contentMin,
                contentMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }

        const int stride = std::max(1, mapping.samplingStride);
        if (visuals_.showGrid && viz_.showCellGrid && mapping.cellW * static_cast<float>(stride) >= 4.0f && mapping.cellH * static_cast<float>(stride) >= 4.0f) {
            const int alpha = static_cast<int>(std::clamp(visuals_.gridOpacity, 0.0f, 1.0f) * 220.0f);
            const float thickness = std::max(0.5f, visuals_.gridLineThickness);
            for (std::uint32_t x = 0; x <= grid.width; x += static_cast<std::uint32_t>(stride)) {
                const float px = mapping.contentMin.x + mapping.cellW * static_cast<float>(x);
                dl.AddLine(ImVec2(px, mapping.viewportMin.y), ImVec2(px, mapping.viewportMax.y), IM_COL32(26, 30, 40, alpha), thickness);
            }
            for (std::uint32_t y = 0; y <= grid.height; y += static_cast<std::uint32_t>(stride)) {
                const float py = mapping.contentMin.y + mapping.cellH * static_cast<float>(y);
                dl.AddLine(ImVec2(mapping.viewportMin.x, py), ImVec2(mapping.viewportMax.x, py), IM_COL32(26, 30, 40, alpha), thickness);
            }
        }

        dl.PopClipRect();

        if (visuals_.showBoundary) {
            const int alpha = static_cast<int>(std::clamp(visuals_.boundaryOpacity, 0.0f, 1.0f) * 255.0f);       
            const float thickness = std::max(0.5f, visuals_.boundaryThickness);
            dl.AddRect(mapping.viewportMin, mapping.viewportMax, IM_COL32(90, 105, 140, alpha), 2.0f, 0, thickness);
        }
        dl.AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), IM_COL32(240, 245, 255, 255), title.c_str());

        if (vp.showLegend) {
            const std::string legend = "min=" + std::to_string(minV) + "  max=" + std::to_string(maxV);
            dl.AddText(ImVec2(min.x + 8.0f, min.y + 26.0f), IM_COL32(210, 220, 245, 255), legend.c_str());
            if (vp.showRangeDetails) {
                const char* modeText =
                    (vp.normalizationMode == NormalizationMode::PerFrameAuto) ? "range=frame" :
                    (vp.normalizationMode == NormalizationMode::StickyPerField) ? "range=sticky" :
                    "range=fixed";
                dl.AddText(ImVec2(min.x + 8.0f, min.y + 44.0f), IM_COL32(180, 200, 240, 230), modeText);
            }
        }
    }

    void drawVectorOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max, const GridSpec grid, const ViewportConfig& vp) const {      
        const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
        if (fields.empty()) {
            return;
        }

        const auto& xField = fields[static_cast<std::size_t>(vp.vectorXFieldIndex)];
        const auto& yField = fields[static_cast<std::size_t>(vp.vectorYFieldIndex)];
        std::vector<float> xValues = mergedFieldValues(xField, viz_.showSparseOverlay);
        std::vector<float> yValues = mergedFieldValues(yField, viz_.showSparseOverlay);

        float xMin = 0.0f;
        float xMax = 1.0f;
        float yMin = 0.0f;
        float yMax = 1.0f;
        minMaxFinite(xValues, xMin, xMax);
        minMaxFinite(yValues, yMin, yMax);

        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const int stride = std::max(std::max(1, vp.vectorStride), mapping.samplingStride);

        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        for (std::uint32_t y = 0; y < grid.height; y += static_cast<std::uint32_t>(stride)) {
            for (std::uint32_t x = 0; x < grid.width; x += static_cast<std::uint32_t>(stride)) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(x);
                if (idx >= xValues.size() || idx >= yValues.size()) {
                    continue;
                }
                if (!std::isfinite(xValues[idx]) || !std::isfinite(yValues[idx])) {
                    continue;
                }

                const float nx = ((xValues[idx] - xMin) / std::max(0.0001f, xMax - xMin) - 0.5f) * 2.0f;
                const float ny = ((yValues[idx] - yMin) / std::max(0.0001f, yMax - yMin) - 0.5f) * 2.0f;

                const ImVec2 center(
                    mapping.contentMin.x + (static_cast<float>(x) + 0.5f) * mapping.cellW,
                    mapping.contentMin.y + (static_cast<float>(y) + 0.5f) * mapping.cellH);
                const float len = std::min(mapping.cellW, mapping.cellH) * static_cast<float>(stride) * vp.vectorScale;
                const ImVec2 tip(center.x + nx * len, center.y + ny * len);
                dl.AddLine(center, tip, IM_COL32(235, 235, 120, 220), 1.4f);
            }
        }

        dl.PopClipRect();
    }

    void clampVisualizationIndices() {
        if (viz_.fieldNames.empty()) {
            return;
        }
        const int maxIndex = static_cast<int>(viz_.fieldNames.size()) - 1;
        for (auto& vp : viz_.viewports) {
            vp.primaryFieldIndex = std::clamp(vp.primaryFieldIndex, 0, maxIndex);
            vp.vectorXFieldIndex = std::clamp(vp.vectorXFieldIndex, 0, maxIndex);
            vp.vectorYFieldIndex = std::clamp(vp.vectorYFieldIndex, 0, maxIndex);
        }
    }

    void refreshFieldNames() {
        std::string message;
        std::vector<std::string> names;
        if (runtime_.fieldNames(names, message) && !names.empty()) {
            viz_.fieldNames = std::move(names);
            for (auto& vp : viz_.viewports) {
                vp.stickyRanges.clear();
            }
            clampVisualizationIndices();
        } else if (!message.empty()) {
            std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", message.c_str());
        }
    }

    void requestSnapshotRefresh() {
        snapshotRequestPending_.store(true);
        viz_.snapshotDirty = true;
    }

    void startSnapshotWorker() {
        stopSnapshotWorker();

        snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));
        snapshotWorkerRunning_.store(true);
        snapshotRequestPending_.store(true);
        snapshotWorker_ = std::thread([this]() { snapshotWorkerLoop(); });
    }

    void stopSnapshotWorker() {
        snapshotWorkerRunning_.store(false);
        if (snapshotWorker_.joinable()) {
            snapshotWorker_.join();
        }
    }

    void snapshotWorkerLoop() {
        using clock = std::chrono::steady_clock;
        auto nextCapture = clock::now();

        while (snapshotWorkerRunning_.load()) {
            const float hz = std::max(1.0f, snapshotRefreshHzAtomic_.load());
            const auto period = std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(hz)));

            const bool continuous = snapshotContinuousMode_.load();
            const auto now = clock::now();
            const bool forced = snapshotRequestPending_.exchange(false);
            if (!forced && !continuous) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
            if (!forced && now < nextCapture) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            RuntimeCheckpoint checkpoint;
            std::string message;
            const auto captureStart = clock::now();
            const bool ok = runtime_.captureCheckpoint(checkpoint, message);
            const float durationMs = static_cast<float>(std::chrono::duration<double, std::milli>(clock::now() - captureStart).count());

            if (ok) {
                const int currentFront = snapshotFrontIndex_.load();
                const int back = 1 - currentFront;
                {
                    const std::lock_guard<std::mutex> lock(snapshotBufferMutex_);
                    snapshotBuffers_[static_cast<std::size_t>(back)] = std::move(checkpoint);
                    snapshotBufferValid_[static_cast<std::size_t>(back)] = true;
                }
                snapshotFrontIndex_.store(back);
                snapshotGeneration_.fetch_add(1);
                snapshotDurationMsAtomic_.store(durationMs);
                {
                    const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
                    snapshotWorkerError_.clear();
                }
            } else if (!message.empty()) {
                const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
                snapshotWorkerError_ = message;
            }

            nextCapture = clock::now() + period;
        }
    }

    void consumeSnapshotFromWorker() {
        snapshotRefreshHzAtomic_.store(std::max(1.0f, viz_.snapshotRefreshHz));

        const int generation = snapshotGeneration_.load();
        if (generation == consumedSnapshotGeneration_) {
            ++viz_.framesSinceSnapshot;
        } else {
            const int front = snapshotFrontIndex_.load();
            RuntimeCheckpoint checkpoint;
            bool hasFrame = false;
            {
                const std::lock_guard<std::mutex> lock(snapshotBufferMutex_);
                if (snapshotBufferValid_[static_cast<std::size_t>(front)]) {
                    checkpoint = snapshotBuffers_[static_cast<std::size_t>(front)];
                    hasFrame = true;
                }
            }

            if (hasFrame) {
                viz_.cachedCheckpoint = std::move(checkpoint);
                viz_.hasCachedCheckpoint = true;
                viz_.snapshotDirty = false;
                viz_.lastSnapshotTimeSec = glfwGetTime();
                viz_.lastSnapshotDurationMs = snapshotDurationMsAtomic_.load();
                viz_.framesSinceSnapshot = 0;
            }

            consumedSnapshotGeneration_ = generation;
        }

        std::string error;
        {
            const std::lock_guard<std::mutex> lock(snapshotErrorMutex_);
            error = snapshotWorkerError_;
        }
        if (!error.empty()) {
            std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", error.c_str());
        } else {
            viz_.lastRuntimeError[0] = '\0';
        }
    }

    void tickAutoRun(const float frameDt) {
        if (appState_ != AppState::Simulation) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(asyncStateMutex_);
            if (asyncErrorPending_) {
                viz_.autoRun = false;
                appendLog(asyncErrorMessage_.empty() ? "auto_run_failed" : asyncErrorMessage_);
                std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", asyncErrorMessage_.c_str());
                asyncErrorPending_ = false;
            }
        }

        snapshotContinuousMode_.store(viz_.autoRun && runtime_.isRunning());

        if (!viz_.autoRun || !runtime_.isRunning() || runtime_.isPaused()) {
            return;
        }

        if (autoRunFuture_.valid() && autoRunFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }

        autoRunStepBudget_ += static_cast<double>(std::max(1, viz_.simulationTickHz)) *
static_cast<double>(std::max(0.0f, frameDt));
        autoRunStepBudget_ = std::min(autoRunStepBudget_, 4.0);
        const int batch = std::min(2, static_cast<int>(std::floor(autoRunStepBudget_)));
        if (batch <= 0) {
            return;
        }

        autoRunStepBudget_ -= static_cast<double>(batch);

        const int stepsToRun = std::min(64, std::max(1, viz_.autoStepsPerFrame * batch));
        autoRunFuture_ = std::async(std::launch::async, [this, stepsToRun]() {
            std::string message;
            if (!runtime_.step(static_cast<std::uint32_t>(stepsToRun), message)) {
                const std::lock_guard<std::mutex> lock(asyncStateMutex_);
                asyncErrorMessage_ = message;
                asyncErrorPending_ = true;
            }
            requestSnapshotRefresh();
        });
    }

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
        drawWorldGenerationSection();
        ImGui::EndChild();

        if (!narrowLayout) {
            ImGui::NextColumn();
        } else {
            ImGui::Spacing();
        }

        SectionHeader("Generation preview", "Preview only — simulation is not running.");
        ImGui::Spacing();
        ImGui::BeginChild("WizardPreview", ImVec2(-1.0f, -1.0f), true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 minPos = ImGui::GetCursorScreenPos();
        const ImVec2 maxPos(minPos.x + avail.x, minPos.y + avail.y);
        dl->AddRectFilled(minPos, maxPos, IM_COL32(12, 14, 24, 255), 4.0f);

        const int previewBaseW = std::max(48, panel_.gridWidth);
        const int previewBaseH = std::max(48, panel_.gridHeight);
        const std::uint64_t previewBudget = 130000ull;
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
        const float cellW = std::max(1.0f, drawW / static_cast<float>(previewW));
        const float cellH = std::max(1.0f, drawH / static_cast<float>(previewH));

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

        for (int y = 0; y < previewH; ++y) {
            for (int x = 0; x < previewW; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(previewW) + static_cast<std::size_t>(x);
                const float value = idx < previewDisplay.values.size() ? previewDisplay.values[idx] : 0.0f;
                const float normalized = std::clamp((value - previewDisplay.minValue) / std::max(0.0001f, previewDisplay.maxValue - previewDisplay.minValue), 0.0f, 1.0f);
                const ImU32 color = mapDisplayTypeColor(
                    (viz_.generationPreviewDisplayType == DisplayType::ScalarField || viz_.generationPreviewDisplayType == DisplayType::WaterDepth) ? normalized : value,
                    viz_.generationPreviewDisplayType,
                    ColorMapMode::Turbo);
                const float px = drawX + static_cast<float>(x) * cellW;
                const float py = drawY + static_cast<float>(y) * cellH;
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + cellW + 0.7f, py + cellH + 0.7f), color);
            }
        }

        const std::string previewLabel = std::string("Preview mode: ") + displayTypeLabel(viz_.generationPreviewDisplayType);
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2), IM_COL32(235, 240, 255, 255), previewLabel.c_str());
        const std::string autoLevel = "Water level: " + std::to_string(previewDisplay.effectiveWaterLevel).substr(0, 5) + (viz_.displayManager.autoWaterLevel ? " (auto)" : " (manual)");
        dl->AddText(ImVec2(minPos.x + kS2, minPos.y + kS2 + 18.0f), IM_COL32(188, 200, 226, 255), autoLevel.c_str());
        if (previewStride > 1) {
            const std::string quality = "Preview stride: 1/" + std::to_string(previewStride) + " for performance";
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
        ImGui::PopStyleVar(3);
        ImGui::End();
    }

    void settingHint(const char* text) const {
        if (text != nullptr && text[0] != '\0') {
            DelayedTooltip(text);
        }
    }

    bool checkboxWithHint(const char* label, bool* value, const char* hint) {
        const bool changed = ImGui::Checkbox(label, value);
        settingHint(hint);
        return changed;
    }

    bool sliderFloatWithHint(const char* label, float* value, const float minV, const float maxV, const char* format, const char* hint) {
        const bool changed = NumericSliderPair(label, value, minV, maxV, format);
        settingHint(hint);
        return changed;
    }

    bool sliderIntWithHint(const char* label, int* value, const int minV, const int maxV, const char* hint) {
        const bool changed = NumericSliderPairInt(label, value, minV, maxV);
        settingHint(hint);
        return changed;
    }

    bool inputTextWithHint(const char* label, char* buffer, const std::size_t size, const char* hint) {
        const bool changed = ImGui::InputText(label, buffer, size);
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
                // Not running at all
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
                // Running (either playing or paused)
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
                    sliderIntWithHint("Simulation Speed (Hz)", &viz_.simulationTickHz, 1, 240, "Target background tick frequency.");
                    sliderIntWithHint("Steps per Tick", &viz_.autoStepsPerFrame, 1, 128, "How many simulation steps are executed each tick.");
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
        if (ImGui::CollapsingHeader("Performance & Sync", 0)) { // Closed by default
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Snapshot mapping time: %.2f ms", viz_.lastSnapshotDurationMs);
            
            sliderFloatWithHint("Snapshot target Hz", &viz_.snapshotRefreshHz, 1.0f, 120.0f, "%.1f", "Target frequency for background snapshot captures.");

            checkboxWithHint("Adaptive Render Sampling", &viz_.adaptiveSampling, "Automatically increases sampling stride when zoomed out to keep rendering responsive.");
            if (!viz_.adaptiveSampling) {
                sliderIntWithHint("Manual stride", &viz_.manualSamplingStride, 1, 64, "Render one cell every N cells when adaptive sampling is disabled.");
            }
            sliderIntWithHint("Max rendered cells", &viz_.maxRenderedCells, 1000, 2000000, "Hard cap on drawn cells per frame across panels.");

            if (PrimaryButton("Apply Interactive Preset", ImVec2(-1.0f, 24.0f))) {
                viz_.simulationTickHz = 40;
                viz_.snapshotRefreshHz = 18.0f;
                viz_.adaptiveSampling = true;
                viz_.manualSamplingStride = 1;
                viz_.maxRenderedCells = 220000;
                viz_.autoStepsPerFrame = 1;
                appendLog("performance_preset=interactive");
                requestSnapshotRefresh();
            }

            if (PrimaryButton("Force Snapshot Refresh", ImVec2(-1.0f, 24.0f))) {
                requestSnapshotRefresh();
            }
        }
        PopSectionTint();
    }

    void drawProfilesAndLogSection() {
        PushSectionTint(2);
        if (ImGui::CollapsingHeader("Profiles & Events", 0)) { // Closed by default
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
                        vp.primaryFieldIndex = i;
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
                if (ImGui::BeginCombo("Primary Field", viz_.fieldNames[static_cast<std::size_t>(vp.primaryFieldIndex)].c_str())) {                                                                                                                  for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
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
            if (ImGui::Combo("Color Palette", &colorMap, colorMapNames.data(), static_cast<int>(colorMapNames.size()))) {                                                                                                                         vp.colorMapMode = static_cast<ColorMapMode>(std::clamp(colorMap, 0, static_cast<int>(colorMapNames.size()) - 1));                                                                                                           }
            settingHint("Transfer palette used to map normalized scalar values for this view.");

            static constexpr std::array<const char*, 3> normalizationNames = {"Per-frame Auto", "Sticky Limit (per-field)", "Fixed Manual Bounds"};                                                                                           int normalization = static_cast<int>(vp.normalizationMode);
            if (ImGui::Combo("Normalization", &normalization, normalizationNames.data(), static_cast<int>(normalizationNames.size()))) {                                                                                                          vp.normalizationMode = static_cast<NormalizationMode>(std::clamp(normalization, 0, static_cast<int>(normalizationNames.size()) - 1));                                                                                       }
            settingHint("Controls how value ranges are normalized before color mapping for this specific view.");

            if (vp.displayType != DisplayType::ScalarField) {
                ImGui::TextDisabled("Category display type selected: scalar normalization/palette are secondary.");
            }

            if (vp.normalizationMode == NormalizationMode::StickyPerField) {
                if (ImGui::Button("Reset Sticky Tracking", ImVec2(170.0f, 24.0f))) {
                    vp.stickyRanges.clear();
                }
                ImGui::SameLine();
                checkboxWithHint("Debug range metrics", &vp.showRangeDetails, "Shows min/max diagnostics for normalization debugging.");                                                                                                    } else if (vp.normalizationMode == NormalizationMode::FixedManual) {
                sliderFloatWithHint("Range Min", &vp.fixedRangeMin, -15.0f, 15.0f, "%.3f", "Lower bound for fixed normalization mode.");                                                                                                        sliderFloatWithHint("Range Max", &vp.fixedRangeMax, -15.0f, 15.0f, "%.3f", "Upper bound for fixed normalization mode.");                                                                                                    }
                
            checkboxWithHint("Show Legend on View", &vp.showLegend, "Displays min/max/value legend overlays where available on this display.");
                
            ImGui::Separator();
            ImGui::TextUnformatted("Data Visual Overlays");
            
            checkboxWithHint("Render Vector Overlay", &vp.showVectorField, "Draws vectors from the selected X/Y scalar fields over this specific Viewport.");                                                                                                            
            if (vp.showVectorField && !viz_.fieldNames.empty()) {
                ImGui::Indent();
                if (ImGui::BeginCombo("Def. X", viz_.fieldNames[static_cast<std::size_t>(vp.vectorXFieldIndex)].c_str())) {                                                                                                                         for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), vp.vectorXFieldIndex == i)) { vp.vectorXFieldIndex = i; }                                                                                     }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Def. Y", viz_.fieldNames[static_cast<std::size_t>(vp.vectorYFieldIndex)].c_str())) {                                                                                                                         for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), vp.vectorYFieldIndex == i)) { vp.vectorYFieldIndex = i; }                                                                                     }
                    ImGui::EndCombo();
                }
                sliderIntWithHint("Ray Stride", &vp.vectorStride, 1, 32, "Sample spacing used when drawing vectors.");                                                                                                                          sliderFloatWithHint("Ray Scale", &vp.vectorScale, 0.05f, 2.0f, "%.2f", "Vector length scale.");
                ImGui::Unindent();
            }
        }
        PopSectionTint();
    }

    void drawOverlaysSection() {
        PushSectionTint(4);
        if (ImGui::CollapsingHeader("Global Spatial Overlays", 0)) { // Closed by default
            checkboxWithHint("Show Domain Boundary", &visuals_.showBoundary, "Draw a boundary rectangle around the visible simulation domain.");                                                                                              if (visuals_.showBoundary) {
                ImGui::Indent();
                sliderFloatWithHint("Opacity", &visuals_.boundaryOpacity, 0.0f, 1.0f, "%.2f", "Boundary line alpha.");                                                                                                                            sliderFloatWithHint("Thickness", &visuals_.boundaryThickness, 0.5f, 6.0f, "%.2f", "Boundary line width in pixels.");                                                                                                              checkboxWithHint("Animate Pulse", &visuals_.boundaryAnimate, "Animates boundary opacity to improve visibility during motion.");                                                                                                   if (accessibility_.reduceMotion) { visuals_.boundaryAnimate = false; }
                ImGui::Unindent();
            }

            checkboxWithHint("Overlay Cell Grid", &visuals_.showGrid, "Draws grid lines over rasterized cells across all displays.");
            if (visuals_.showGrid) {
                viz_.showCellGrid = true;
                ImGui::Indent();
                sliderFloatWithHint("Grid Op", &visuals_.gridOpacity, 0.0f, 1.0f, "%.2f", "Grid line opacity."); 
                sliderFloatWithHint("Grid W", &visuals_.gridLineThickness, 0.5f, 4.0f, "%.2f", "Grid line thickness in pixels.");                                                                                                                 ImGui::Unindent();
            } else {
                viz_.showCellGrid = false;
            }

            ImGui::Separator();
            checkboxWithHint("Render Sparse Objects", &viz_.showSparseOverlay, "Includes sparse overlay entries in the rasterized display.");                                                                                             }
        PopSectionTint();
    }
    void drawOpticsSection() {
        PushSectionTint(5);
        if (ImGui::CollapsingHeader("Camera & Optics", 0)) { // Closed by default
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
        if (ImGui::CollapsingHeader("Environmental Physics", 0)) { // Closed by default
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
        if (ImGui::CollapsingHeader("Cockpit Accessibility", 0)) { // Closed by default
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
    std::atomic<float> snapshotRefreshHzAtomic_{12.0f};
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
    std::array<RenderCacheState, 4> renderCaches_{};
    int maxTextureSize_ = 0;

    double autoRunStepBudget_ = 0.0;
    RuntimeService runtime_;
};

} // namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui


