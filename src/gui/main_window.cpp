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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <string>
#include <thread>
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
    int gridWidth = 128;
    int gridHeight = 128;
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

    float terrainBaseFrequency = 2.2f;
    float terrainDetailFrequency = 7.5f;
    float terrainWarpStrength = 0.55f;
    float terrainAmplitude = 1.0f;
    float terrainRidgeMix = 0.28f;
    float seaLevel = 0.48f;
    float polarCooling = 0.62f;
    float humidityFromWater = 0.52f;
    float biomeNoiseStrength = 0.20f;

    char profileName[128] = "baseline";
    char summaryVariable[128] = "temperature_T";
    char checkpointLabel[128] = "quick";
};

struct OverlayState {
    float alpha = 0.0f;
    OverlayIcon icon = OverlayIcon::None;
};

enum class DisplayMode {
    Single = 0,
    SideBySideVertical = 1,
    SideBySideHorizontal = 2,
    MixedOverlay = 3
};

enum class NormalizationMode {
    PerFrameAuto = 0,
    StickyPerField = 1,
    FixedManual = 2
};

enum class ColorMapMode {
    Turbo = 0,
    Grayscale = 1,
    Diverging = 2
};

struct VisualizationState {
    DisplayMode mode = DisplayMode::Single;
    bool showCellGrid = false;
    bool showLegend = true;
    bool showVectorField = false;
    bool showSparseOverlay = true;
    bool autoRun = false;
    int autoStepsPerFrame = 1;
    int simulationTickHz = 30;
    float snapshotRefreshHz = 12.0f;
    bool adaptiveSampling = true;
    int manualSamplingStride = 1;
    int maxRenderedCells = 180000;
    int vectorStride = 6;
    float vectorScale = 0.45f;
    float secondaryBlend = 0.45f;
    NormalizationMode normalizationMode = NormalizationMode::StickyPerField;
    ColorMapMode colorMapMode = ColorMapMode::Turbo;
    float fixedRangeMin = 0.0f;
    float fixedRangeMax = 1.0f;
    bool showRangeDetails = true;

    int primaryFieldIndex = 0;
    int secondaryFieldIndex = 1;
    int vectorXFieldIndex = 4;
    int vectorYFieldIndex = 3;

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

            drawViewport();
            drawSimulationCanvas();
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

        stopSnapshotWorker();

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
        const std::string& fieldName,
        const float localMin,
        const float localMax) {
        if (viz_.normalizationMode == NormalizationMode::FixedManual) {
            const float lo = std::min(viz_.fixedRangeMin, viz_.fixedRangeMax);
            const float hi = std::max(viz_.fixedRangeMin, viz_.fixedRangeMax);
            return {lo, std::max(lo + 1e-6f, hi)};
        }

        if (viz_.normalizationMode == NormalizationMode::PerFrameAuto) {
            return {localMin, localMax};
        }

        auto it = viz_.stickyRanges.find(fieldName);
        if (it == viz_.stickyRanges.end()) {
            viz_.stickyRanges.emplace(fieldName, std::make_pair(localMin, localMax));
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

    [[nodiscard]] ImU32 mapColor(const float normalizedValue) const {
        const float t = applyDisplayTransfer(normalizedValue, visuals_);
        switch (viz_.colorMapMode) {
            case ColorMapMode::Grayscale:
                return colormapGrayscale(t);
            case ColorMapMode::Diverging:
                return colormapDiverging(t);
            case ColorMapMode::Turbo:
            default:
                return colormapTurboLike(t);
        }
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
        const auto& primaryField = fields[static_cast<std::size_t>(viz_.primaryFieldIndex)];
        const auto& secondaryField = fields[static_cast<std::size_t>(viz_.secondaryFieldIndex)];

        std::vector<float> primaryValues = mergedFieldValues(primaryField, viz_.showSparseOverlay);
        std::vector<float> secondaryValues = mergedFieldValues(secondaryField, viz_.showSparseOverlay);

        float pMin = 0.0f;
        float pMax = 1.0f;
        float sMin = 0.0f;
        float sMax = 1.0f;
        minMaxFinite(primaryValues, pMin, pMax);
        minMaxFinite(secondaryValues, sMin, sMax);
        const auto pRange = resolveDisplayRange(primaryField.spec.name, pMin, pMax);
        const auto sRange = resolveDisplayRange(secondaryField.spec.name, sMin, sMax);
        pMin = pRange.first;
        pMax = pRange.second;
        sMin = sRange.first;
        sMax = sRange.second;

        if (viz_.mode == DisplayMode::Single) {
            drawScalarFieldPanel(*dl, canvasMin, canvasMax, snapshot.grid, primaryField.spec.name, primaryValues, pMin, pMax);
        } else if (viz_.mode == DisplayMode::SideBySideVertical) {
            const float midX = (canvasMin.x + canvasMax.x) * 0.5f;
            drawScalarFieldPanel(*dl, canvasMin, ImVec2(midX - 3.0f, canvasMax.y), snapshot.grid, primaryField.spec.name, primaryValues, pMin, pMax);
            drawScalarFieldPanel(*dl, ImVec2(midX + 3.0f, canvasMin.y), canvasMax, snapshot.grid, secondaryField.spec.name, secondaryValues, sMin, sMax);
        } else if (viz_.mode == DisplayMode::SideBySideHorizontal) {
            const float midY = (canvasMin.y + canvasMax.y) * 0.5f;
            drawScalarFieldPanel(*dl, canvasMin, ImVec2(canvasMax.x, midY - 3.0f), snapshot.grid, primaryField.spec.name, primaryValues, pMin, pMax);
            drawScalarFieldPanel(*dl, ImVec2(canvasMin.x, midY + 3.0f), canvasMax, snapshot.grid, secondaryField.spec.name, secondaryValues, sMin, sMax);
        } else {
            drawMixedPanel(*dl, canvasMin, canvasMax, snapshot.grid, primaryField.spec.name, secondaryField.spec.name, primaryValues, secondaryValues, pMin, pMax, sMin, sMax);
        }

        if (viz_.showVectorField) {
            drawVectorOverlay(*dl, canvasMin, canvasMax, snapshot.grid);
        }
    }

    void drawScalarFieldPanel(
        ImDrawList& dl,
        const ImVec2 min,
        const ImVec2 max,
        const GridSpec grid,
        const std::string& title,
        const std::vector<float>& values,
        const float minV,
        const float maxV) const {
        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const float width = mapping.viewportMax.x - mapping.viewportMin.x;
        const float height = mapping.viewportMax.y - mapping.viewportMin.y;
        if (width <= 4.0f || height <= 4.0f || grid.width == 0 || grid.height == 0) {
            return;
        }

        dl.AddRectFilled(mapping.viewportMin, mapping.viewportMax, IM_COL32(12, 14, 22, 255), 2.0f);
        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        const int stride = std::max(1, mapping.samplingStride);
        for (std::uint32_t y = 0; y < grid.height; y += static_cast<std::uint32_t>(stride)) {
            for (std::uint32_t x = 0; x < grid.width; x += static_cast<std::uint32_t>(stride)) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(x);
                const float value = idx < values.size() ? values[idx] : std::numeric_limits<float>::quiet_NaN();
                ImU32 color = IM_COL32(18, 18, 28, 255);
                if (std::isfinite(value)) {
                    const float t = (value - minV) / (maxV - minV);
                    color = mapColor(t);
                }

                const ImVec2 a(
                    mapping.contentMin.x + mapping.cellW * static_cast<float>(x),
                    mapping.contentMin.y + mapping.cellH * static_cast<float>(y));
                const ImVec2 b(
                    a.x + mapping.cellW * static_cast<float>(stride) + 0.75f,
                    a.y + mapping.cellH * static_cast<float>(stride) + 0.75f);
                dl.AddRectFilled(a, b, color);
            }
        }

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

        if (viz_.showLegend) {
            const std::string legend = "min=" + std::to_string(minV) + "  max=" + std::to_string(maxV);
            dl.AddText(ImVec2(min.x + 8.0f, min.y + 26.0f), IM_COL32(210, 220, 245, 255), legend.c_str());
            if (viz_.showRangeDetails) {
                const char* modeText =
                    (viz_.normalizationMode == NormalizationMode::PerFrameAuto) ? "range=frame" :
                    (viz_.normalizationMode == NormalizationMode::StickyPerField) ? "range=sticky" :
                    "range=fixed";
                dl.AddText(ImVec2(min.x + 8.0f, min.y + 44.0f), IM_COL32(180, 200, 240, 230), modeText);
            }
        }
    }

    void drawMixedPanel(
        ImDrawList& dl,
        const ImVec2 min,
        const ImVec2 max,
        const GridSpec grid,
        const std::string& pName,
        const std::string& sName,
        const std::vector<float>& primary,
        const std::vector<float>& secondary,
        const float pMin,
        const float pMax,
        const float sMin,
        const float sMax) const {
        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const int stride = std::max(1, mapping.samplingStride);

        dl.AddRectFilled(mapping.viewportMin, mapping.viewportMax, IM_COL32(12, 14, 22, 255), 2.0f);
        dl.PushClipRect(mapping.viewportMin, mapping.viewportMax, true);

        for (std::uint32_t y = 0; y < grid.height; y += static_cast<std::uint32_t>(stride)) {
            for (std::uint32_t x = 0; x < grid.width; x += static_cast<std::uint32_t>(stride)) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(x);
                const float pv = idx < primary.size() ? primary[idx] : std::numeric_limits<float>::quiet_NaN();
                const float sv = idx < secondary.size() ? secondary[idx] : std::numeric_limits<float>::quiet_NaN();

                const float p = std::isfinite(pv) ? std::clamp((pv - pMin) / (pMax - pMin), 0.0f, 1.0f) : 0.0f;
                const float s = std::isfinite(sv) ? std::clamp((sv - sMin) / (sMax - sMin), 0.0f, 1.0f) : 0.0f;

                const float pTone = applyDisplayTransfer(p, visuals_);
                const float sTone = applyDisplayTransfer(s, visuals_);

                const float mixA = std::clamp(viz_.secondaryBlend, 0.0f, 1.0f);
                const int r = static_cast<int>((1.0f - mixA) * pTone * 255.0f + mixA * sTone * 90.0f);
                const int g = static_cast<int>((1.0f - mixA) * (0.4f + 0.6f * pTone) * 255.0f + mixA * (0.5f + 0.5f * sTone) * 180.0f);
                const int b = static_cast<int>((1.0f - mixA) * (1.0f - pTone) * 220.0f + mixA * sTone * 255.0f);

                const ImVec2 a(
                    mapping.contentMin.x + mapping.cellW * static_cast<float>(x),
                    mapping.contentMin.y + mapping.cellH * static_cast<float>(y));
                const ImVec2 bRect(
                    a.x + mapping.cellW * static_cast<float>(stride) + 0.75f,
                    a.y + mapping.cellH * static_cast<float>(stride) + 0.75f);
                dl.AddRectFilled(a, bRect, IM_COL32(r, g, b, 255));
            }
        }

        dl.PopClipRect();

        if (visuals_.showBoundary) {
            const int alpha = static_cast<int>(std::clamp(visuals_.boundaryOpacity, 0.0f, 1.0f) * 255.0f);
            const float thickness = std::max(0.5f, visuals_.boundaryThickness);
            dl.AddRect(mapping.viewportMin, mapping.viewportMax, IM_COL32(90, 105, 140, alpha), 2.0f, 0, thickness);
        }
        const std::string title = "Mixed: " + pName + " + " + sName;
        dl.AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), IM_COL32(240, 245, 255, 255), title.c_str());
    }

    void drawVectorOverlay(ImDrawList& dl, const ImVec2 min, const ImVec2 max, const GridSpec grid) const {
        const auto& fields = viz_.cachedCheckpoint.stateSnapshot.fields;
        if (fields.empty()) {
            return;
        }

        const auto& xField = fields[static_cast<std::size_t>(viz_.vectorXFieldIndex)];
        const auto& yField = fields[static_cast<std::size_t>(viz_.vectorYFieldIndex)];
        std::vector<float> xValues = mergedFieldValues(xField, viz_.showSparseOverlay);
        std::vector<float> yValues = mergedFieldValues(yField, viz_.showSparseOverlay);

        float xMin = 0.0f;
        float xMax = 1.0f;
        float yMin = 0.0f;
        float yMax = 1.0f;
        minMaxFinite(xValues, xMin, xMax);
        minMaxFinite(yValues, yMin, yMax);

        const ViewMapping mapping = makeViewMapping(min, max, grid);
        const int stride = std::max(std::max(1, viz_.vectorStride), mapping.samplingStride);

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

                const float nx = ((xValues[idx] - xMin) / (xMax - xMin) - 0.5f) * 2.0f;
                const float ny = ((yValues[idx] - yMin) / (yMax - yMin) - 0.5f) * 2.0f;

                const ImVec2 center(
                    mapping.contentMin.x + (static_cast<float>(x) + 0.5f) * mapping.cellW,
                    mapping.contentMin.y + (static_cast<float>(y) + 0.5f) * mapping.cellH);
                const float len = std::min(mapping.cellW, mapping.cellH) * static_cast<float>(stride) * viz_.vectorScale;
                const ImVec2 tip(center.x + nx * len, center.y + ny * len);
                dl.AddLine(center, tip, IM_COL32(235, 235, 120, 220), 1.4f);
            }
        }

        dl.PopClipRect();
    }

    void clampVisualizationIndices() {
        if (viz_.fieldNames.empty()) {
            viz_.primaryFieldIndex = 0;
            viz_.secondaryFieldIndex = 0;
            viz_.vectorXFieldIndex = 0;
            viz_.vectorYFieldIndex = 0;
            return;
        }

        const int maxIndex = static_cast<int>(viz_.fieldNames.size()) - 1;
        viz_.primaryFieldIndex = std::clamp(viz_.primaryFieldIndex, 0, maxIndex);
        viz_.secondaryFieldIndex = std::clamp(viz_.secondaryFieldIndex, 0, maxIndex);
        viz_.vectorXFieldIndex = std::clamp(viz_.vectorXFieldIndex, 0, maxIndex);
        viz_.vectorYFieldIndex = std::clamp(viz_.vectorYFieldIndex, 0, maxIndex);
    }

    void refreshFieldNames() {
        std::string message;
        std::vector<std::string> names;
        if (runtime_.fieldNames(names, message) && !names.empty()) {
            viz_.fieldNames = std::move(names);
            viz_.stickyRanges.clear();
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
        snapshotContinuousMode_.store(viz_.autoRun && runtime_.isRunning());

        if (!viz_.autoRun || !runtime_.isRunning() || runtime_.isPaused()) {
            return;
        }

        autoRunStepBudget_ += static_cast<double>(std::max(1, viz_.simulationTickHz)) * static_cast<double>(std::max(0.0f, frameDt));
        autoRunStepBudget_ = std::min(autoRunStepBudget_, 4.0);
        const int batch = std::min(2, static_cast<int>(std::floor(autoRunStepBudget_)));
        if (batch <= 0) {
            return;
        }

        autoRunStepBudget_ -= static_cast<double>(batch);

        const int stepsToRun = std::min(64, std::max(1, viz_.autoStepsPerFrame * batch));
        std::string message;
        if (!runtime_.step(static_cast<std::uint32_t>(stepsToRun), message)) {
            viz_.autoRun = false;
            appendLog(message.empty() ? "auto_run_failed" : message);
            std::snprintf(viz_.lastRuntimeError, sizeof(viz_.lastRuntimeError), "%s", message.c_str());
            return;
        }
        requestSnapshotRefresh();
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

    void drawControlPanel() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, 850.0f), ImGuiCond_FirstUseEver);

        ImGui::Begin("Control Panel");

        drawStatusHeader();

        if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Session")) {
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
                drawGridSetupSection();
                drawWorldGenerationSection();
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
        
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 200.0f);

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

    void drawSessionLifecycleSection() {
        PushSectionTint(0);
        if (ImGui::CollapsingHeader("Engine Lifecycle & Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (PrimaryButton("Start", ImVec2(100, 28))) {
                std::string message;
                runtime_.start(message);
                appendLog(message);
                refreshFieldNames();
                requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Play);
            }
            ImGui::SameLine();
            if (PrimaryButton("Stop", ImVec2(100, 28))) {
                std::string message;
                runtime_.stop(message);
                appendLog(message);
                requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Pause);
            }
            ImGui::SameLine();
            if (PrimaryButton("Restart", ImVec2(100, 28))) {
                std::string message;
                runtime_.restart(message);
                appendLog(message);
                refreshFieldNames();
                requestSnapshotRefresh();
                triggerOverlay(OverlayIcon::Play);
            }

            ImGui::Separator();

            checkboxWithHint("Auto-run simulation", &viz_.autoRun, "Continuously advances the simulation at the configured tick rate. Disable for manual stepping.");
            if (viz_.autoRun) {
                sliderIntWithHint("Steps / tick", &viz_.autoStepsPerFrame, 1, 128, "How many simulation steps are executed each tick while auto-run is enabled.");
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
                    requestSnapshotRefresh();
                    triggerOverlay(OverlayIcon::Play);
                }
            } else {
                sliderIntWithHint("Step Count", &panel_.stepCount, 1, 1000000, "Number of steps to execute when pressing Run Step(s).");
                if (PrimaryButton("Run Step(s)", ImVec2(120, 26))) {
                    std::string message;
                    runtime_.step(static_cast<std::uint32_t>(panel_.stepCount), message);
                    appendLog(message);
                    requestSnapshotRefresh();
                }

                ImGui::Separator();
                int runUntil = static_cast<int>(std::min<std::uint64_t>(panel_.runUntilTarget, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
                if (sliderIntWithHint("Run Until", &runUntil, 0, kImGuiIntSafeMax, "Advance simulation until this absolute step index is reached.")) {
                    panel_.runUntilTarget = static_cast<std::uint64_t>(runUntil);
                }
                if (PrimaryButton("Run Continuous", ImVec2(120, 26))) {
                    std::string message;
                    runtime_.runUntil(panel_.runUntilTarget, message);
                    appendLog(message);
                    requestSnapshotRefresh();
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
            
            sliderIntWithHint("Simulation tick Hz", &viz_.simulationTickHz, 1, 240, "Target simulation frequency used by auto-run.");
            sliderFloatWithHint("Snapshot target Hz", &viz_.snapshotRefreshHz, 1.0f, 120.0f, "%.1f", "Target frequency for background snapshot captures.");

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
            for (const std::string& line : logs_) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        PopSectionTint();
    }

    void drawDisplayMappingSection() {
        PushSectionTint(3);
        if (ImGui::CollapsingHeader("Continuum Field Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            static constexpr std::array<const char*, 4> modeNames = {
                "Single Field",
                "Side-by-Side (Vertical)",
                "Side-by-Side (Horizontal)",
                "Mixed Overlay"
            };
            int mode = static_cast<int>(viz_.mode);
            if (ImGui::Combo("Display Mode", &mode, modeNames.data(), static_cast<int>(modeNames.size()))) {
                viz_.mode = static_cast<DisplayMode>(std::clamp(mode, 0, static_cast<int>(modeNames.size()) - 1));
            }
            settingHint("How scalar fields are arranged on the simulation canvas.");

            if (PrimaryButton("Refresh Field List", ImVec2(160.0f, 24.0f))) {
                refreshFieldNames();
            }

            if (!viz_.fieldNames.empty()) {
                clampVisualizationIndices();
                if (ImGui::BeginCombo("Primary Field", viz_.fieldNames[static_cast<std::size_t>(viz_.primaryFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        const bool selected = (viz_.primaryFieldIndex == i);
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), selected)) {
                            viz_.primaryFieldIndex = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                settingHint("Main scalar variable displayed in the first panel.");

                if (viz_.mode != DisplayMode::Single) {
                    if (ImGui::BeginCombo("Secondary Field", viz_.fieldNames[static_cast<std::size_t>(viz_.secondaryFieldIndex)].c_str())) {
                        for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                            const bool selected = (viz_.secondaryFieldIndex == i);
                            if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), selected)) {
                                viz_.secondaryFieldIndex = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    settingHint("Secondary scalar variable used by split or mixed display modes.");
                }

                if (viz_.mode == DisplayMode::MixedOverlay) {
                    sliderFloatWithHint("Overlay Blend", &viz_.secondaryBlend, 0.0f, 1.0f, "%.2f", "Blend weight of the secondary field in mixed overlay mode.");
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Colorization & Ranges");

            static constexpr std::array<const char*, 3> colorMapNames = {"Turbo", "Grayscale", "Diverging"};
            int colorMap = static_cast<int>(viz_.colorMapMode);
            if (ImGui::Combo("Color Palette", &colorMap, colorMapNames.data(), static_cast<int>(colorMapNames.size()))) {
                viz_.colorMapMode = static_cast<ColorMapMode>(std::clamp(colorMap, 0, static_cast<int>(colorMapNames.size()) - 1));
            }
            settingHint("Transfer palette used to map normalized scalar values to color.");

            static constexpr std::array<const char*, 3> normalizationNames = {"Per-frame Auto", "Sticky Limit (per-field)", "Fixed Manual Bounds"};
            int normalization = static_cast<int>(viz_.normalizationMode);
            if (ImGui::Combo("Normalization", &normalization, normalizationNames.data(), static_cast<int>(normalizationNames.size()))) {
                viz_.normalizationMode = static_cast<NormalizationMode>(std::clamp(normalization, 0, static_cast<int>(normalizationNames.size()) - 1));
            }
            settingHint("Controls how value ranges are normalized before color mapping.");

            if (viz_.normalizationMode == NormalizationMode::StickyPerField) {
                if (ImGui::Button("Reset Sticky Tracking", ImVec2(170.0f, 24.0f))) {
                    viz_.stickyRanges.clear();
                }
                ImGui::SameLine();
                checkboxWithHint("Debug range metrics", &viz_.showRangeDetails, "Shows min/max diagnostics for normalization debugging.");
            } else if (viz_.normalizationMode == NormalizationMode::FixedManual) {
                sliderFloatWithHint("Range Min", &viz_.fixedRangeMin, -15.0f, 15.0f, "%.3f", "Lower bound for fixed normalization mode.");
                sliderFloatWithHint("Range Max", &viz_.fixedRangeMax, -15.0f, 15.0f, "%.3f", "Upper bound for fixed normalization mode.");
            }
        }
        PopSectionTint();
    }

    void drawOverlaysSection() {
        PushSectionTint(4);
        if (ImGui::CollapsingHeader("Vector & Spatial Overlays", 0)) { // Closed by default
            checkboxWithHint("Show Domain Boundary", &visuals_.showBoundary, "Draw a boundary rectangle around the visible simulation domain.");
            if (visuals_.showBoundary) {
                ImGui::Indent();
                sliderFloatWithHint("Opacity", &visuals_.boundaryOpacity, 0.0f, 1.0f, "%.2f", "Boundary line alpha.");
                sliderFloatWithHint("Thickness", &visuals_.boundaryThickness, 0.5f, 6.0f, "%.2f", "Boundary line width in pixels.");
                checkboxWithHint("Animate Pulse", &visuals_.boundaryAnimate, "Animates boundary opacity to improve visibility during motion.");
                if (accessibility_.reduceMotion) { visuals_.boundaryAnimate = false; }
                ImGui::Unindent();
            }

            checkboxWithHint("Overlay Cell Grid", &visuals_.showGrid, "Draws grid lines over rasterized cells.");
            if (visuals_.showGrid) {
                viz_.showCellGrid = true;
                ImGui::Indent();
                sliderFloatWithHint("Grid Op", &visuals_.gridOpacity, 0.0f, 1.0f, "%.2f", "Grid line opacity.");
                sliderFloatWithHint("Grid W", &visuals_.gridLineThickness, 0.5f, 4.0f, "%.2f", "Grid line thickness in pixels.");
                ImGui::Unindent();
            } else {
                viz_.showCellGrid = false;
            }

            checkboxWithHint("Render Vector Fields", &viz_.showVectorField, "Draws vectors from the selected X/Y scalar fields.");
            if (viz_.showVectorField && !viz_.fieldNames.empty()) {
                ImGui::Indent();
                clampVisualizationIndices();
                if (ImGui::BeginCombo("Def. X", viz_.fieldNames[static_cast<std::size_t>(viz_.vectorXFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), viz_.vectorXFieldIndex == i)) { viz_.vectorXFieldIndex = i; }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Def. Y", viz_.fieldNames[static_cast<std::size_t>(viz_.vectorYFieldIndex)].c_str())) {
                    for (int i = 0; i < static_cast<int>(viz_.fieldNames.size()); ++i) {
                        if (ImGui::Selectable(viz_.fieldNames[static_cast<std::size_t>(i)].c_str(), viz_.vectorYFieldIndex == i)) { viz_.vectorYFieldIndex = i; }
                    }
                    ImGui::EndCombo();
                }
                sliderIntWithHint("Ray Stride", &viz_.vectorStride, 1, 32, "Sample spacing used when drawing vectors.");
                sliderFloatWithHint("Ray Scale", &viz_.vectorScale, 0.05f, 2.0f, "%.2f", "Vector length scale.");
                ImGui::Unindent();
            }

            ImGui::Separator();
            checkboxWithHint("Show Value Legend", &viz_.showLegend, "Displays min/max/value legend overlays where available.");
            checkboxWithHint("Render Sparse Objects", &viz_.showSparseOverlay, "Includes sparse overlay entries in the rasterized display.");
        }
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
            int seed = static_cast<int>(std::min<std::uint64_t>(panel_.seed, static_cast<std::uint64_t>(kImGuiIntSafeMax)));
            if (sliderIntWithHint("Entropy Seed", &seed, 0, kImGuiIntSafeMax, "Primary deterministic seed for world generation and initialization.")) {
                panel_.seed = static_cast<std::uint64_t>(seed);
            }

            sliderIntWithHint("Width (Cols)", &panel_.gridWidth, 1, 1024, "Grid width in cells.");
            sliderIntWithHint("Height (Rows)", &panel_.gridHeight, 1, 1024, "Grid height in cells.");

            if (ImGui::BeginCombo("Runtime Tier", kTierOptions[panel_.tierIndex])) {
                for (int i = 0; i < static_cast<int>(kTierOptions.size()); ++i) {
                    if (ImGui::Selectable(kTierOptions[i], panel_.tierIndex == i)) { panel_.tierIndex = i; }
                }
                ImGui::EndCombo();
            }
            settingHint("Model family tier controlling coupling complexity (A/B/C).");

            if (ImGui::BeginCombo("Temporal Mode", kTemporalOptions[panel_.temporalIndex])) {
                for (int i = 0; i < static_cast<int>(kTemporalOptions.size()); ++i) {
                    if (ImGui::Selectable(kTemporalOptions[i], panel_.temporalIndex == i)) { panel_.temporalIndex = i; }
                }
                ImGui::EndCombo();
            }
            settingHint("Temporal integration policy used by the scheduler.");

            ImGui::Spacing();
            if (PrimaryButton("Apply Configuration & Reset", ImVec2(-1.0f, 32.0f))) {
                applyConfigFromPanel();
            }
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
            ImGui::TextUnformatted("Climate and Hydrology");
            sliderFloatWithHint("Sea Level", &panel_.seaLevel, 0.0f, 1.0f, "%.3f", "Waterline threshold used to derive initial water coverage.");
            sliderFloatWithHint("Polar Cooling", &panel_.polarCooling, 0.0f, 1.5f, "%.2f", "Strength of temperature cooling away from warm latitudes.");
            sliderFloatWithHint("Humidity from Water", &panel_.humidityFromWater, 0.0f, 1.5f, "%.2f", "Influence of water presence on humidity initialization.");
            sliderFloatWithHint("Biome Noise", &panel_.biomeNoiseStrength, 0.0f, 1.0f, "%.2f", "Additional noise contribution to biome/temperature diversity.");

            ImGui::Spacing();
            ImGui::TextDisabled("These settings apply on Start/Restart or after applying config.");
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
        panel_.seaLevel = config.worldGen.seaLevel;
        panel_.polarCooling = config.worldGen.polarCooling;
        panel_.humidityFromWater = config.worldGen.humidityFromWater;
        panel_.biomeNoiseStrength = config.worldGen.biomeNoiseStrength;
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

        config.worldGen.terrainBaseFrequency = panel_.terrainBaseFrequency;
        config.worldGen.terrainDetailFrequency = panel_.terrainDetailFrequency;
        config.worldGen.terrainWarpStrength = panel_.terrainWarpStrength;
        config.worldGen.terrainAmplitude = panel_.terrainAmplitude;
        config.worldGen.terrainRidgeMix = panel_.terrainRidgeMix;
        config.worldGen.seaLevel = panel_.seaLevel;
        config.worldGen.polarCooling = panel_.polarCooling;
        config.worldGen.humidityFromWater = panel_.humidityFromWater;
        config.worldGen.biomeNoiseStrength = panel_.biomeNoiseStrength;

        runtime_.setConfig(config);

        std::ostringstream output;
        output << "config_applied seed=" << config.seed
               << " grid=" << config.grid.width << 'x' << config.grid.height
               << " tier=" << toString(config.tier)
             << " temporal=" << app::temporalPolicyToString(config.temporalPolicy)
             << " gen.base_freq=" << config.worldGen.terrainBaseFrequency
             << " gen.detail_freq=" << config.worldGen.terrainDetailFrequency
             << " gen.sea_level=" << config.worldGen.seaLevel;
        appendLog(output.str());
        requestSnapshotRefresh();
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
    VisualizationState viz_{};
    OverlayState overlay_{};
    std::vector<std::string> logs_;

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

    double autoRunStepBudget_ = 0.0;
    RuntimeService runtime_;
};

} // namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui
