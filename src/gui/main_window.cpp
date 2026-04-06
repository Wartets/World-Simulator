#include "ws/gui/main_window.hpp"

#include "ws/gui/display_manager.hpp"
#include "ws/gui/main_window/color_utils.hpp"
#include "ws/gui/main_window/detail_utils.hpp"
#include "ws/gui/main_window/panel_state.hpp"
#include "ws/gui/histogram_panel.hpp"
#include "ws/gui/heatmap_renderer.hpp"
#include "ws/gui/contour_renderer.hpp"
#include "ws/gui/constraint_monitor.hpp"
#include "ws/gui/event_logger.hpp"
#include "ws/gui/parameter_panel.hpp"
#include "ws/gui/perturbation_panel.hpp"
#include "ws/gui/render_rules.hpp"
#include "ws/gui/model_editor_window.hpp"
#include "ws/gui/model_selector.hpp"
#include "ws/core/model_parser.hpp"
#include "ws/gui/runtime_service.hpp"
#include "ws/gui/session_manager/session_manager.hpp"
enum class AppState        { ModelSelector, ModelEditor, SessionManager, NewWorldWizard, Simulation };
#include "ws/gui/theme_bootstrap.hpp"
#include "ws/gui/time_control_panel.hpp"
#include "ws/gui/timeseries_panel.hpp"
#include "ws/gui/ui_components.hpp"
#include "ws/gui/vector_renderer.hpp"
#include "ws/gui/viewport_manager.hpp"
#include "ws/gui/generation_advisor.hpp"

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
enum class AppState        { ModelSelector, ModelEditor, SessionManager, NewWorldWizard, Simulation };

struct ViewportConfig {
    int  primaryFieldIndex = 0;
    DisplayType displayType = DisplayType::ScalarField;
    ViewportRenderMode renderMode = ViewportRenderMode::Heatmap;

    NormalizationMode normalizationMode = NormalizationMode::StickyPerField;
    ColorMapMode      colorMapMode      = ColorMapMode::Turbo;
    HeatmapNormalization heatmapNormalization = HeatmapNormalization::Linear;
    HeatmapColorMap      heatmapColorMap = HeatmapColorMap::Turbo;
    float heatmapPowerExponent = 1.0f;
    float heatmapQuantileLow   = 0.05f;
    float heatmapQuantileHigh  = 0.95f;
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

    bool  showContours = false;
    float contourInterval = 0.1f;
    int   contourMaxLevels = 24;

    bool  customRuleEnabled = false;
};

struct VisualizationState {
    static constexpr std::size_t kDefaultViewportCount = 2;

    [[nodiscard]] static ViewportConfig makeDefaultViewportConfig(const std::size_t index) {
        ViewportConfig cfg;
        cfg.primaryFieldIndex = static_cast<int>(index);
        cfg.colorMapMode = (index == 1u) ? ColorMapMode::Water : ColorMapMode::Turbo;
        return cfg;
    }

    ScreenLayout layout = ScreenLayout::SplitLeftRight;
    std::vector<ViewportConfig> viewports = []() {
        std::vector<ViewportConfig> configs;
        configs.reserve(kDefaultViewportCount);
        for (std::size_t i = 0; i < kDefaultViewportCount; ++i) {
            configs.push_back(makeDefaultViewportConfig(i));
        }
        return configs;
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

constexpr std::array<const char*, 3> kTierOptions    = {
    "Local deterministic",
    "Neighborhood exchange",
    "Coupled multi-rate"
};
constexpr std::array<const char*, 3> kTemporalOptions = {
    "Single-pass",
    "Phased",
    "Adaptive multi-rate"
};
constexpr std::array<const char*, 3> kTemporalPolicyTokens = {"uniform","phased","multirate"};
constexpr int   kImGuiIntSafeMax = std::numeric_limits<int>::max() / 2;
constexpr float kS2 = 8.0f;
constexpr float kS3 = 12.0f;
constexpr float kS5 = 24.0f;
constexpr float kPageMaxWidth = 1600.0f;

[[nodiscard]] const char* generationModeLabel(const InitialConditionType type) {
    switch (type) {
        case InitialConditionType::Terrain: return "Geographic Terrain";
        case InitialConditionType::Conway: return "Conway's Life (Random)";
        case InitialConditionType::GrayScott: return "Gray-Scott (Spots)";
        case InitialConditionType::Waves: return "Waves (Drop)";
        case InitialConditionType::Blank: return "Blank Grid";
        case InitialConditionType::Voronoi: return "Voronoi Cells";
        case InitialConditionType::Clustering: return "Clustered Seeds";
        case InitialConditionType::SparseRandom: return "Sparse Random";
        case InitialConditionType::GradientField: return "Gradient Field";
        case InitialConditionType::Checkerboard: return "Checkerboard";
        case InitialConditionType::RadialPattern: return "Radial Pattern";
        case InitialConditionType::MultiScale: return "Multi-Scale Noise";
        case InitialConditionType::DiffusionLimit: return "Diffusion-Limited";
        default: return "Unknown";
    }
}

[[nodiscard]] std::string humanizeToken(const std::string& token) {
    std::string out;
    out.reserve(token.size());
    bool upperNext = true;
    for (char ch : token) {
        if (ch == '_' || ch == '-') {
            out.push_back(' ');
            upperNext = true;
            continue;
        }
        if (upperNext) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            upperNext = false;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

[[nodiscard]] bool containsToken(const std::string& haystack, const std::initializer_list<const char*> needles) {
    const auto normalize = [](const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (char ch : value) {
            const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
                out.push_back(lower);
            }
        }
        return out;
    };

    const std::string norm = normalize(haystack);
    for (const char* needle : needles) {
        if (needle == nullptr) {
            continue;
        }
        const std::string n = normalize(std::string(needle));
        if (!n.empty() && norm.find(n) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool isRuntimeSupportedGenerationMode(const InitialConditionType mode) {
    switch (mode) {
        case InitialConditionType::Terrain:
        case InitialConditionType::Conway:
        case InitialConditionType::GrayScott:
        case InitialConditionType::Waves:
        case InitialConditionType::Blank:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] InitialConditionType fallbackRuntimeSupportedMode(const InitialConditionType mode) {
    if (isRuntimeSupportedGenerationMode(mode)) {
        return mode;
    }
    if (mode == InitialConditionType::SparseRandom || mode == InitialConditionType::Clustering) {
        return InitialConditionType::Conway;
    }
    if (mode == InitialConditionType::GradientField || mode == InitialConditionType::RadialPattern) {
        return InitialConditionType::Waves;
    }
    return InitialConditionType::Blank;
}

[[nodiscard]] float applyVariableRestriction(const session_manager::VariableInitializationSetting& setting, const float value) {
    switch (setting.restrictionMode) {
        case 1:
            return std::clamp(value, std::min(setting.clampMin, setting.clampMax), std::max(setting.clampMin, setting.clampMax));
        case 2:
            return std::max(0.0f, value);
        case 3:
            return std::clamp(value, -1.0f, 1.0f);
        case 4:
            return std::tanh(value);
        case 5:
            return 1.0f / (1.0f + std::exp(-value));
        default:
            return value;
    }
}

[[nodiscard]] InitialConditionType refineRecommendedModeForKnownModels(
    const initialization::ModelVariableCatalog& catalog,
    const InitialConditionType advisorMode) {
    const std::string modelId = catalog.modelId;
    if (containsToken(modelId, {"game_of_life", "conway"})) {
        return InitialConditionType::Conway;
    }
    if (containsToken(modelId, {"gray_scott", "reaction_diffusion"})) {
        return InitialConditionType::GrayScott;
    }
    if (containsToken(modelId, {"shallow_water", "navier_stokes", "fluid"})) {
        return InitialConditionType::Waves;
    }
    if (containsToken(modelId, {"forest_fire"})) {
        return InitialConditionType::SparseRandom;
    }
    if (containsToken(modelId, {"predator_prey"})) {
        return InitialConditionType::Clustering;
    }
    if (containsToken(modelId, {"urban_microclimate", "environmental", "coastal"})) {
        return InitialConditionType::Terrain;
    }
    return advisorMode;
}

[[nodiscard]] DisplayType recommendedPreviewDisplayTypeForMode(const InitialConditionType mode) {
    switch (mode) {
        case InitialConditionType::Waves:
            return DisplayType::WaterDepth;
        case InitialConditionType::GrayScott:
            return DisplayType::MoistureMap;
        case InitialConditionType::Conway:
            return DisplayType::ScalarField;
        case InitialConditionType::Terrain:
            return DisplayType::SurfaceCategory;
        default:
            return DisplayType::ScalarField;
    }
}

[[nodiscard]] int recommendedPreviewSourceForMode(const InitialConditionType mode) {
    switch (mode) {
        case InitialConditionType::Terrain: return 2; // terrain proxy
        case InitialConditionType::GrayScott: return 3; // water proxy
        case InitialConditionType::Waves: return 1; // primary signal
        case InitialConditionType::Conway: return 1; // primary signal
        default: return 0; // auto
    }
}

[[nodiscard]] int findPreferredVariableIndex(
    const std::vector<std::string>& variables,
    const std::initializer_list<const char*> tokens,
    const int fallbackIndex = 0) {
    for (int i = 0; i < static_cast<int>(variables.size()); ++i) {
        if (containsToken(variables[static_cast<std::size_t>(i)], tokens)) {
            return i;
        }
    }
    return std::clamp(fallbackIndex, 0, std::max(0, static_cast<int>(variables.size()) - 1));
}

void applyAutoVariableBindingsForMode(
    PanelState& panel,
    const std::vector<std::string>& cellVariables,
    const InitialConditionType modeType) {
    if (cellVariables.empty()) {
        return;
    }

    if (modeType == InitialConditionType::Conway) {
        const int idx = findPreferredVariableIndex(cellVariables, {"state", "living", "fire_state", "vegetation"});
        std::snprintf(panel.conwayTargetVariable, sizeof(panel.conwayTargetVariable), "%s", cellVariables[static_cast<std::size_t>(idx)].c_str());
    } else if (modeType == InitialConditionType::GrayScott) {
        const int aIdx = findPreferredVariableIndex(cellVariables, {"u_concentration", "resource", "nutrient", "prey"});
        int bIdx = findPreferredVariableIndex(cellVariables, {"v_concentration", "vegetation", "biomass", "predator"}, std::min(1, static_cast<int>(cellVariables.size()) - 1));
        if (bIdx == aIdx && cellVariables.size() > 1) {
            bIdx = (aIdx + 1) % static_cast<int>(cellVariables.size());
        }
        std::snprintf(panel.grayScottTargetVariableA, sizeof(panel.grayScottTargetVariableA), "%s", cellVariables[static_cast<std::size_t>(aIdx)].c_str());
        std::snprintf(panel.grayScottTargetVariableB, sizeof(panel.grayScottTargetVariableB), "%s", cellVariables[static_cast<std::size_t>(bIdx)].c_str());
    } else if (modeType == InitialConditionType::Waves) {
        const int idx = findPreferredVariableIndex(cellVariables, {"water", "height", "surface", "velocity", "wave"});
        std::snprintf(panel.wavesTargetVariable, sizeof(panel.wavesTargetVariable), "%s", cellVariables[static_cast<std::size_t>(idx)].c_str());
    }
}

void rebuildVariableInitializationSettings(
    session_manager::SessionUiState& sessionUi,
    const initialization::ModelVariableCatalog& catalog) {
    sessionUi.variableInitializationSettings.clear();
    const auto cellVars = catalog.cellStateVariableIds();
    sessionUi.variableInitializationSettings.reserve(cellVars.size());
    for (const auto& variableId : cellVars) {
        session_manager::VariableInitializationSetting setting;
        setting.variableId = variableId;
        setting.enabled = false;
        setting.baseValue = 0.0f;
        setting.restrictionMode = 0;
        setting.clampMin = 0.0f;
        setting.clampMax = 1.0f;
        for (const auto& descriptor : catalog.variables) {
            if (descriptor.id == variableId) {
                if (descriptor.hasDomainMin) {
                    setting.clampMin = descriptor.domainMin;
                }
                if (descriptor.hasDomainMax) {
                    setting.clampMax = descriptor.domainMax;
                }
                if (descriptor.hasDomainMin || descriptor.hasDomainMax) {
                    setting.restrictionMode = 1;
                }
                break;
            }
        }
        sessionUi.variableInitializationSettings.push_back(std::move(setting));
    }
}

void applyGenerationDefaultsForMode(
    PanelState& panel,
    const initialization::ModelVariableCatalog& catalog,
    const InitialConditionType modeType,
    const bool selectMode) {
    if (selectMode) {
        panel.initialConditionTypeIndex = static_cast<int>(modeType);
    }

    const auto defaults = GenerationAdvisor::recommendDefaultParameters(catalog, modeType);

    panel.terrainBaseFrequency = defaults.terrainBaseFrequency;
    panel.terrainDetailFrequency = defaults.terrainDetailFrequency;
    panel.terrainWarpStrength = defaults.terrainWarpStrength;
    panel.terrainAmplitude = defaults.terrainAmplitude;
    panel.terrainRidgeMix = defaults.terrainRidgeMix;
    panel.terrainOctaves = defaults.terrainOctaves;
    panel.terrainLacunarity = defaults.terrainLacunarity;
    panel.terrainGain = defaults.terrainGain;
    panel.seaLevel = defaults.seaLevel;
    panel.polarCooling = defaults.polarCooling;
    panel.latitudeBanding = defaults.latitudeBanding;
    panel.humidityFromWater = defaults.humidityFromWater;
    panel.biomeNoiseStrength = defaults.biomeNoiseStrength;
    panel.islandDensity = defaults.islandDensity;
    panel.islandFalloff = defaults.islandFalloff;
    panel.coastlineSharpness = defaults.coastlineSharpness;
    panel.archipelagoJitter = defaults.archipelagoJitter;
    panel.erosionStrength = defaults.erosionStrength;
    panel.shelfDepth = defaults.shelfDepth;

    panel.conwayAliveProbability = defaults.conwayAliveProbability;
    panel.conwayAliveValue = defaults.conwayAliveValue;
    panel.conwayDeadValue = defaults.conwayDeadValue;
    panel.conwaySmoothingPasses = defaults.conwaySmoothingPasses;

    panel.grayScottBackgroundA = defaults.grayScottBackgroundA;
    panel.grayScottBackgroundB = defaults.grayScottBackgroundB;
    panel.grayScottSpotValueA = defaults.grayScottSpotValueA;
    panel.grayScottSpotValueB = defaults.grayScottSpotValueB;
    panel.grayScottSpotCount = defaults.grayScottSpotCount;
    panel.grayScottSpotRadius = defaults.grayScottSpotRadius;
    panel.grayScottSpotJitter = defaults.grayScottSpotJitter;

    panel.waveBaseline = defaults.waveBaseline;
    panel.waveDropAmplitude = defaults.waveDropAmplitude;
    panel.waveDropRadius = defaults.waveDropRadius;
    panel.waveDropCount = defaults.waveDropCount;
    panel.waveDropJitter = defaults.waveDropJitter;
    panel.waveRingFrequency = defaults.waveRingFrequency;
}

// Helper: Apply generation advisor recommendations to panel state
void applyGenerationAdvisorRecommendations(
    PanelState& panel,
    const initialization::ModelVariableCatalog& catalog,
    const std::vector<ParameterControl>& parameters) {
    const auto modeRec = GenerationAdvisor::recommendGenerationMode(catalog, parameters);
    applyGenerationDefaultsForMode(panel, catalog, modeRec.recommendedType, true);
}

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
        heatmapRenderer_.initialize();
        requestSnapshotRefresh();
        startSnapshotWorker();
        startSimulationWorker();

        const auto openModelInEditor = [this](const std::filesystem::path& modelPath, const char* errorPrefix) {
            if (!std::filesystem::exists(modelPath)) {
                appendLog(std::string(errorPrefix) + "_path_missing=" + modelPath.string());
                return;
            }

            modelEditor_.setActiveModelPath(modelPath);

            try {
                const ModelContext context = ws::ModelParser::load(modelPath);
                modelEditor_.loadModel(context);
            } catch (const std::exception& strictError) {
                appendLog(std::string(errorPrefix) + "_strict=" + strictError.what());
                try {
                    ModelContext fallback;
                    if (std::filesystem::is_directory(modelPath)) {
                        fallback = ws::ModelParser::loadFromDirectory(modelPath);
                    } else {
                        fallback = ws::ModelParser::loadFromZip(modelPath);
                    }
                    modelEditor_.loadModel(fallback);
                    appendLog(std::string(errorPrefix) + "_fallback=raw_context_loaded");
                } catch (const std::exception& fallbackError) {
                    appendLog(std::string(errorPrefix) + "_fallback_error=" + fallbackError.what());
                    return;
                }
            }

            modelSelector_.close();
            modelEditor_.open();
            appState_ = AppState::ModelEditor;
        };

        modelSelector_.on_edit_model = [openModelInEditor](const ModelInfo& model) {
            openModelInEditor(model.path, "model_load_error");
        };
        modelSelector_.on_load_model = [this](const ModelInfo& model) {
            modelSelector_.close();
            modelEditor_.close();
            runtime_.setModelScope(ModelScopeContext{
                model.model_id.empty() ? model.name : model.model_id,
                model.name,
                model.path,
                model.identity_hash});
            sessionUi_.needsRefresh = true;
            std::snprintf(sessionUi_.selectedModelName, sizeof(sessionUi_.selectedModelName), "%s", model.name.c_str());
            std::snprintf(
                sessionUi_.selectedModelDescription,
                sizeof(sessionUi_.selectedModelDescription),
                "%s",
                model.description.empty() ? "No description available." : model.description.c_str());
            std::snprintf(
                sessionUi_.selectedModelAuthor,
                sizeof(sessionUi_.selectedModelAuthor),
                "%s",
                model.author.empty() ? "Unknown" : model.author.c_str());
            std::snprintf(
                sessionUi_.selectedModelVersion,
                sizeof(sessionUi_.selectedModelVersion),
                "%s",
                model.version.empty() ? "unknown" : model.version.c_str());
            std::snprintf(
                sessionUi_.selectedModelPath,
                sizeof(sessionUi_.selectedModelPath),
                "%s",
                model.path.c_str());

            sessionUi_.selectedModelCatalog = initialization::ModelVariableCatalog{};
            sessionUi_.selectedModelCellStateVariables.clear();
            {
                std::string catalogMessage;
                if (initialization::loadModelVariableCatalog(model.path, sessionUi_.selectedModelCatalog, catalogMessage)) {
                    sessionUi_.selectedModelCellStateVariables = sessionUi_.selectedModelCatalog.cellStateVariableIds();
                    const auto modeRec = GenerationAdvisor::recommendGenerationMode(sessionUi_.selectedModelCatalog, {});
                    const InitialConditionType refinedMode = refineRecommendedModeForKnownModels(
                        sessionUi_.selectedModelCatalog,
                        modeRec.recommendedType);
                    sessionUi_.generationModeIndex = static_cast<int>(refinedMode);

                    applyGenerationDefaultsForMode(panel_, sessionUi_.selectedModelCatalog, refinedMode, true);
                    applyAutoVariableBindingsForMode(panel_, sessionUi_.selectedModelCellStateVariables, refinedMode);

                    viz_.generationPreviewDisplayType = recommendedPreviewDisplayTypeForMode(refinedMode);
                    sessionUi_.generationPreviewSourceIndex = recommendedPreviewSourceForMode(refinedMode);
                    sessionUi_.generationPreviewChannelIndex = findPreferredVariableIndex(
                        sessionUi_.selectedModelCellStateVariables,
                        {"water", "state", "concentration", "temperature", "vegetation"},
                        0);

                    rebuildVariableInitializationSettings(sessionUi_, sessionUi_.selectedModelCatalog);

                    std::string recLog = "Auto-recommended: " +
                        std::string(generationModeLabel(refinedMode)) +
                        " (" + std::to_string(static_cast<int>(modeRec.confidence * 100.0f)) + "% confidence, reason=" +
                        humanizeToken(modeRec.rationale) + ")";
                    appendLog(recLog);
                } else {
                    appendLog(catalogMessage);
                }
            }

            sessionUi_.generationBindingPlan = initialization::InitializationBindingPlan{};
            sessionUi_.allowUnresolvedGenerationBindings = false;
            sessionUi_.allowHeavyInitializationWork = false;
            sessionUi_.generationShowOnlyViableModes = false;
            if (sessionUi_.selectedModelCellStateVariables.empty()) {
                sessionUi_.generationPreviewSourceIndex = 0;
                sessionUi_.generationPreviewChannelIndex = 0;
            }
            std::snprintf(sessionUi_.statusMessage, sizeof(sessionUi_.statusMessage), "model_selected=%s", model.name.c_str());
            appState_ = AppState::SessionManager;
        };
        modelSelector_.on_model_created = [openModelInEditor](const std::string& modelName) {
            openModelInEditor(modelName, "model_create_error");
        };

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            tickAutoRun();
            consumeSnapshotFromWorker();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            uiParameterChangedThisFrame_    = false;
            uiParameterInteractingThisFrame_ = false;

            if (appState_ == AppState::ModelSelector) {
                drawModelSelector();
            } else if (appState_ == AppState::ModelEditor) {
                drawModelEditor();
            } else if (appState_ == AppState::Simulation) {
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
            glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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

    void drawModelSelector() {
        modelSelector_.render(ImGui::GetIO().DisplaySize);
    }

    void drawModelEditor() {
        if (!modelEditor_.isOpen()) {
            appState_ = AppState::ModelSelector;
            modelSelector_.open();
            return;
        }

        modelEditor_.render(ImGui::GetIO().DisplaySize);
        if (!modelEditor_.isOpen()) {
            appState_ = AppState::ModelSelector;
            modelSelector_.open();
        }
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
    std::string asyncWarningMessage_;
    bool asyncWarningPending_ = false;

    ModelSelector      modelSelector_{};
    ModelEditorWindow  modelEditor_{"Model Editor"};
    AppState           appState_ = AppState::ModelSelector;
    std::unordered_map<std::string, std::vector<std::string>> fieldDisplayTags_{};

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
    std::vector<RasterTexture> viewportRasters_{};
    std::vector<std::vector<std::uint8_t>> rasterBuffers_{};
    RasterTexture         wizardPreviewTexture_{};
    std::vector<std::uint8_t> wizardPreviewPixels_{};
    std::uint64_t         wizardPreviewHash_     = 0;
    int                   wizardPreviewW_         = 0;
    int                   wizardPreviewH_         = 0;
    int                   wizardPreviewStride_    = 1;
    float                 wizardPreviewWaterLevel_ = 0.0f;
    std::vector<RenderCacheState> renderCaches_{};
    std::unordered_map<std::uint64_t, DisplayBuffer> snapshotDisplayCache_;
    int snapshotDisplayCacheGeneration_ = -1;
    int maxTextureSize_ = 0;
    int nextViewportRebuildCursor_ = 0;
    int viewportTabSelectionRequest_ = -1;
    int lastFocusedRuntimeViewport_ = -1;

    HeatmapRenderer heatmapRenderer_{};
    ViewportManager viewportManager_{VisualizationState::kDefaultViewportCount};
    VectorRenderer  vectorRenderer_{};
    ContourRenderer contourRenderer_{};
    std::vector<std::vector<RenderRule>> viewportRenderRules_{};
    bool dockLayoutInitialized_ = false;

    // UI interaction state
    bool   uiParameterChangedThisFrame_    = false;
    bool   uiParameterInteractingThisFrame_ = false;
    double uiInteractionHotUntilSec_       = 0.0;
    double lastAutoRunWatchdogLogSec_ = -1.0;

    RuntimeService runtime_;
    ConstraintMonitor constraintMonitor_{};
    std::uint64_t recordedDiagnosticsStep_ = std::numeric_limits<std::uint64_t>::max();
};

} // anonymous namespace

int MainWindow::run() {
    MainWindowImpl impl;
    return impl.run();
}

} // namespace ws::gui
