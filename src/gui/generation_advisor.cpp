#include "ws/gui/generation_advisor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ws::gui {

namespace {

// Creates lowercase copy of string for case-insensitive comparison.
// @param value Input string
// @return Lowercase version of string
std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// Checks if tag list contains specified tag (case-insensitive).
// @param tags Vector of tags to search
// @param needle Tag to find
// @return true if tag found
bool tagsContain(const std::vector<std::string>& tags, const std::string& needle) {
    const std::string needleLower = toLowerCopy(needle);
    return std::any_of(tags.begin(), tags.end(), [&](const std::string& tag) {
        return toLowerCopy(tag) == needleLower;
    });
}

// Checks if hint list contains specified hint (case-insensitive).
// @param hints Vector of hints to search
// @param needle Hint to find
// @return true if hint found
bool hintsContain(const std::vector<std::string>& hints, const std::string& needle) {
    const std::string needleLower = toLowerCopy(needle);
    return std::any_of(hints.begin(), hints.end(), [&](const std::string& hint) {
        return toLowerCopy(hint) == needleLower;
    });
}

bool modeRequiresCellVariables(const InitialConditionType mode) {
    switch (mode) {
        case InitialConditionType::Blank:
            return false;
        default:
            return true;
    }
}

bool containsMode(const std::vector<InitialConditionType>& modes, const InitialConditionType mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

bool modeSupportedByCatalog(const initialization::ModelVariableCatalog& catalog, const InitialConditionType mode) {
    if (catalog.supportedInitializationModes.empty()) {
        return true;
    }
    return std::find(
               catalog.supportedInitializationModes.begin(),
               catalog.supportedInitializationModes.end(),
               mode) != catalog.supportedInitializationModes.end();
}

}

bool GenerationAdvisor::catalogHasVariablesWithTag(
    const initialization::ModelVariableCatalog& catalog,
    const std::string& tag) {
    for (const auto& variable : catalog.variables) {
        if (tagsContain(variable.tags, tag)) {
            return true;
        }
    }
    return false;
}

bool GenerationAdvisor::catalogHasVariablesWithHint(
    const initialization::ModelVariableCatalog& catalog,
    const std::string& hint) {
    for (const auto& variable : catalog.variables) {
        if (hintsContain(variable.initializationHints, hint)) {
            return true;
        }
    }
    return false;
}

int GenerationAdvisor::countVariablesWithRole(
    const initialization::ModelVariableCatalog& catalog,
    const std::string& role) {
    int count = 0;
    const std::string roleLower = toLowerCopy(role);
    for (const auto& variable : catalog.variables) {
        if (variable.support == "cell" && toLowerCopy(variable.role) == roleLower) {
            ++count;
        }
    }
    return count;
}

int GenerationAdvisor::countVariablesWithTag(
    const initialization::ModelVariableCatalog& catalog,
    const std::string& tag) {
    int count = 0;
    for (const auto& variable : catalog.variables) {
        if (variable.support == "cell" && tagsContain(variable.tags, tag)) {
            ++count;
        }
    }
    return count;
}

float GenerationAdvisor::estimateModelComplexity(
    const initialization::ModelVariableCatalog& catalog,
    const std::vector<ParameterControl>& parameters) {
    const int variableCount = static_cast<int>(catalog.variables.size());
    const int cellVariables = static_cast<int>(catalog.cellVariableIds().size());
    const int parameterCount = static_cast<int>(parameters.size());

    float complexity = 0.0f;
    complexity += static_cast<float>(variableCount) * 0.1f;
    complexity += static_cast<float>(cellVariables) * 0.3f;
    complexity += static_cast<float>(parameterCount) * 0.2f;

    if (catalogHasVariablesWithTag(catalog, "vector")) {
        complexity += 0.5f;
    }
    if (catalogHasVariablesWithTag(catalog, "energy") || catalogHasVariablesWithTag(catalog, "momentum")) {
        complexity += 0.4f;
    }
    if (countVariablesWithRole(catalog, "state") > 3) {
        complexity += 0.3f;
    }

    return complexity;
}

GenerationModeRecommendation GenerationAdvisor::recommendGenerationMode(
    const initialization::ModelVariableCatalog& catalog,
    const std::vector<ParameterControl>& parameters) {
    GenerationModeRecommendation result;
    result.recommendedType = InitialConditionType::Blank;
    result.confidence = 0.4f;
    result.rationale = "insufficient_model_signal";

    if (catalog.variables.empty()) {
        result.rationale = "empty_variable_catalog";
        return result;
    }

    const float complexity = estimateModelComplexity(catalog, parameters);
    const int cellVarCount = static_cast<int>(catalog.cellVariableIds().size());
    const int totalVarCount = static_cast<int>(catalog.variables.size());

    if (catalog.preferredInitializationMode.has_value()) {
        const InitialConditionType preferredMode = *catalog.preferredInitializationMode;
        if (!modeSupportedByCatalog(catalog, preferredMode)) {
            result.recommendedType = InitialConditionType::Blank;
            result.confidence = 0.50f;
            result.rationale = "metadata_preferred_mode_not_supported";
            return result;
        }
        if (modeRequiresCellVariables(preferredMode) && cellVarCount == 0) {
            result.recommendedType = InitialConditionType::Blank;
            result.confidence = 0.65f;
            result.rationale = "metadata_mode_requires_cell_variables";
            return result;
        }

        result.recommendedType = preferredMode;
        result.confidence = 0.99f;
        result.rationale = "metadata_preferred_initialization_mode";
        return result;
    }

    // Priority 1: Explicit hints and exact matches
    if (catalogHasVariablesWithHint(catalog, "conway")) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Conway)) {
            goto skip_explicit_conway;
        }
        result.recommendedType = InitialConditionType::Conway;
        result.confidence = 0.98f;
        result.rationale = "explicit_conway_hint_in_metadata";
        return result;
    }
skip_explicit_conway:
    if (catalogHasVariablesWithHint(catalog, "gray_scott")) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::GrayScott)) {
            goto skip_explicit_grayscott;
        }
        result.recommendedType = InitialConditionType::GrayScott;
        result.confidence = 0.98f;
        result.rationale = "explicit_gray_scott_hint_in_metadata";
        return result;
    }
skip_explicit_grayscott:

    // Priority 2: Terrain-like signals (geography, climate, spatial)
    if (catalogHasVariablesWithTag(catalog, "elevation") ||
        catalogHasVariablesWithTag(catalog, "terrain") ||
        catalogHasVariablesWithTag(catalog, "altitude")) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Terrain)) {
            goto skip_terrain_tag;
        }
        result.recommendedType = InitialConditionType::Terrain;
        result.confidence = 0.96f;
        result.rationale = "explicit_terrain_or_elevation_tag";
        return result;
    }
skip_terrain_tag:
    if (catalogHasVariablesWithTag(catalog, "climate") ||
        catalogHasVariablesWithTag(catalog, "temperature") ||
        catalogHasVariablesWithTag(catalog, "precipitation")) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Terrain)) {
            goto skip_climate_terrain;
        }
        result.recommendedType = InitialConditionType::Terrain;
        result.confidence = 0.90f;
        result.rationale = "climate_system_detected";
        return result;
    }
skip_climate_terrain:

    // Priority 3: Reaction-diffusion signals
    if ((catalogHasVariablesWithTag(catalog, "concentration") ||
         catalogHasVariablesWithTag(catalog, "activator") ||
         catalogHasVariablesWithTag(catalog, "inhibitor")) &&
        cellVarCount >= 2) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::GrayScott)) {
            goto skip_reaction_diffusion;
        }
        result.recommendedType = InitialConditionType::GrayScott;
        result.confidence = 0.90f;
        result.rationale = "reaction_diffusion_pattern_detected";
        return result;
    }
skip_reaction_diffusion:

    // Priority 4: Wave/fluid systems
    if (catalogHasVariablesWithTag(catalog, "wave") ||
        catalogHasVariablesWithTag(catalog, "fluid") ||
        (catalogHasVariablesWithTag(catalog, "velocity") && cellVarCount >= 2)) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Waves)) {
            goto skip_wave_system;
        }
        result.recommendedType = InitialConditionType::Waves;
        result.confidence = 0.88f;
        result.rationale = "wave_or_fluid_dynamics_detected";
        return result;
    }
skip_wave_system:

    // Priority 5: Population/cellular automata
    if (catalogHasVariablesWithTag(catalog, "population") ||
        catalogHasVariablesWithTag(catalog, "cell_state") ||
        (catalogHasVariablesWithTag(catalog, "state") && cellVarCount <= 3)) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Conway)) {
            goto skip_population_mode;
        }
        result.recommendedType = InitialConditionType::Conway;
        result.confidence = 0.82f;
        result.rationale = "population_or_state_automata_detected";
        return result;
    }
skip_population_mode:

    // Priority 6: Structured pattern modes based on complexity and variable count
    if (cellVarCount >= 3 && complexity < 0.6f) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Voronoi)) {
            goto skip_voronoi;
        }
        result.recommendedType = InitialConditionType::Voronoi;
        result.confidence = 0.75f;
        result.rationale = "simple_multi_variable_suggests_tessellation";
        return result;
    }
skip_voronoi:

    if (cellVarCount >= 2 && complexity >= 1.0f && complexity < 1.8f) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Clustering)) {
            goto skip_clustering;
        }
        result.recommendedType = InitialConditionType::Clustering;
        result.confidence = 0.70f;
        result.rationale = "moderate_complexity_multi_variable_suggests_clustering";
        return result;
    }
skip_clustering:

    if (cellVarCount == 1 && complexity < 0.4f) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::SparseRandom)) {
            goto skip_sparse_random;
        }
        result.recommendedType = InitialConditionType::SparseRandom;
        result.confidence = 0.68f;
        result.rationale = "simple_single_variable_suggests_sparse_seeding";
        return result;
    }
skip_sparse_random:

    if (catalogHasVariablesWithTag(catalog, "field") && cellVarCount >= 1) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::GradientField)) {
            goto skip_gradient;
        }
        result.recommendedType = InitialConditionType::GradientField;
        result.confidence = 0.72f;
        result.rationale = "field_variable_detected";
        return result;
    }
skip_gradient:

    // Priority 7: Multi-variable complexity handling
    if (cellVarCount >= 4) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::MultiScale)) {
            goto skip_multiscale;
        }
        result.recommendedType = InitialConditionType::MultiScale;
        result.confidence = 0.65f;
        result.rationale = "high_variable_count_suggests_multi_scale";
        return result;
    }
skip_multiscale:

    if (cellVarCount >= 2) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Clustering)) {
            goto skip_multi_var_cluster;
        }
        result.recommendedType = InitialConditionType::Clustering;
        result.confidence = 0.60f;
        result.rationale = "multiple_cell_variables_suggest_spatial_clustering";
        return result;
    }
skip_multi_var_cluster:

    // Fallback: simple geometry patterns for minimal models
    if (cellVarCount == 1 && totalVarCount <= 2) {
        if (!modeSupportedByCatalog(catalog, InitialConditionType::Checkerboard)) {
            goto skip_checkerboard;
        }
        result.recommendedType = InitialConditionType::Checkerboard;
        result.confidence = 0.55f;
        result.rationale = "minimal_model_suggest_geometric_pattern";
        return result;
    }
skip_checkerboard:

    if (!catalog.supportedInitializationModes.empty()) {
        for (const InitialConditionType mode : catalog.supportedInitializationModes) {
            if (!modeRequiresCellVariables(mode) || cellVarCount > 0) {
                result.recommendedType = mode;
                result.confidence = 0.52f;
                result.rationale = "metadata_supported_mode_fallback";
                return result;
            }
        }
    }

    // Ultimate fallback
    result.recommendedType = InitialConditionType::Blank;
    result.confidence = 0.40f;
    result.rationale = "no_matching_pattern_detected";
    return result;
}

GenerationParameterDefaults GenerationAdvisor::recommendDefaultParameters(
    const initialization::ModelVariableCatalog& catalog,
    const InitialConditionType modeType) {
    GenerationParameterDefaults defaults;

    const float complexity = estimateModelComplexity(catalog, {});
    const int variableCount = static_cast<int>(catalog.variables.size());
    const int cellVarCount = static_cast<int>(catalog.cellVariableIds().size());

    // ===== TERRAIN =====
    if (modeType == InitialConditionType::Terrain) {
        if (complexity > 1.8f) {
            defaults.terrainBaseFrequency = 0.6f;
            defaults.terrainDetailFrequency = 3.0f;
            defaults.terrainOctaves = 2;
            defaults.islandDensity = 0.6f;
            defaults.erosionStrength = 0.8f;
        } else if (complexity > 1.2f) {
            defaults.terrainBaseFrequency = 1.0f;
            defaults.terrainDetailFrequency = 5.0f;
            defaults.terrainOctaves = 3;
            defaults.islandDensity = 0.5f;
            defaults.erosionStrength = 0.6f;
        } else if (complexity < 0.4f) {
            defaults.terrainBaseFrequency = 3.0f;
            defaults.terrainDetailFrequency = 9.0f;
            defaults.terrainOctaves = 6;
            defaults.islandDensity = 0.2f;
            defaults.erosionStrength = 0.1f;
        }

        if (catalogHasVariablesWithTag(catalog, "temperature") ||
            catalogHasVariablesWithTag(catalog, "humidity")) {
            defaults.polarCooling = 1.2f;
            defaults.latitudeBanding = 1.3f;
            defaults.humidityFromWater = 1.0f;
            defaults.biomeNoiseStrength = 0.8f;
        }

        if (catalogHasVariablesWithTag(catalog, "coastline") ||
            catalogHasVariablesWithTag(catalog, "shelf")) {
            defaults.coastlineSharpness = 2.2f;
            defaults.shelfDepth = 0.6f;
            defaults.archipelagoJitter = 0.7f;
        }
    }

    // ===== CONWAY =====
    if (modeType == InitialConditionType::Conway) {
        if (complexity > 1.5f) {
            defaults.conwayAliveProbability = 0.30f;
            defaults.conwaySmoothingPasses = 0;
        } else if (complexity > 0.8f) {
            defaults.conwayAliveProbability = 0.35f;
            defaults.conwaySmoothingPasses = 0;
        } else {
            defaults.conwayAliveProbability = 0.40f;
            defaults.conwaySmoothingPasses = 0;
        }
    }

    // ===== GRAY-SCOTT =====
    if (modeType == InitialConditionType::GrayScott) {
        if (cellVarCount > 5) {
            defaults.grayScottSpotCount = 15;
            defaults.grayScottSpotRadius = 25.0f;
        } else if (cellVarCount > 3) {
            defaults.grayScottSpotCount = 10;
            defaults.grayScottSpotRadius = 18.0f;
        } else {
            defaults.grayScottSpotCount = 6;
            defaults.grayScottSpotRadius = 12.0f;
        }

        if (catalogHasVariablesWithTag(catalog, "inhibitor")) {
            defaults.grayScottBackgroundA = 0.7f;
            defaults.grayScottBackgroundB = 0.05f;
        }
        if (catalogHasVariablesWithTag(catalog, "catalyst")) {
            defaults.grayScottSpotJitter = 0.1f;
        }
    }

    // ===== WAVES =====
    if (modeType == InitialConditionType::Waves) {
        if (complexity > 1.2f) {
            defaults.waveDropCount = 6;
            defaults.waveDropRadius = 45.0f;
            defaults.waveRingFrequency = 1.2f;
        } else if (complexity > 0.6f) {
            defaults.waveDropCount = 4;
            defaults.waveDropRadius = 32.0f;
            defaults.waveRingFrequency = 1.8f;
        } else {
            defaults.waveDropCount = 2;
            defaults.waveDropRadius = 20.0f;
            defaults.waveRingFrequency = 2.5f;
        }

        if (catalogHasVariablesWithTag(catalog, "energy")) {
            defaults.waveDropAmplitude = 1.0f;
        }
    }

    // ===== VORONOI =====
    if (modeType == InitialConditionType::Voronoi) {
        if (cellVarCount > 4) {
            defaults.voronoiSeedCount = 20;
            defaults.voronoiSmoothing = 0.5f;
        } else if (cellVarCount > 2) {
            defaults.voronoiSeedCount = 15;
            defaults.voronoiSmoothing = 0.4f;
        } else {
            defaults.voronoiSeedCount = 10;
            defaults.voronoiSmoothing = 0.2f;
        }
        defaults.voronoiJitter = 0.4f + 0.2f * (complexity / 2.0f);
    }

    // ===== CLUSTERING =====
    if (modeType == InitialConditionType::Clustering) {
        if (complexity > 1.5f) {
            defaults.clusteringClusterCount = 12;
            defaults.clusteringClusterRadius = 25.0f;
            defaults.clusteringClusterDecay = 0.7f;
        } else if (complexity > 0.8f) {
            defaults.clusteringClusterCount = 8;
            defaults.clusteringClusterRadius = 20.0f;
            defaults.clusteringClusterDecay = 0.6f;
        } else {
            defaults.clusteringClusterCount = 5;
            defaults.clusteringClusterRadius = 15.0f;
            defaults.clusteringClusterDecay = 0.5f;
        }
        defaults.clusteringClusterIntensity = 0.8f;
        defaults.clusteringClusterSpread = std::clamp(0.3f + 0.1f * cellVarCount, 0.2f, 0.8f);
    }

    // ===== SPARSE RANDOM =====
    if (modeType == InitialConditionType::SparseRandom) {
        if (complexity > 1.0f) {
            defaults.sparseRandomFillDensity = 0.25f;
            defaults.sparseRandomClusterSparse = true;
            defaults.sparseRandomClusterRadius = 12.0f;
        } else if (complexity > 0.5f) {
            defaults.sparseRandomFillDensity = 0.15f;
            defaults.sparseRandomClusterSparse = true;
            defaults.sparseRandomClusterRadius = 8.0f;
        } else {
            defaults.sparseRandomFillDensity = 0.08f;
            defaults.sparseRandomClusterSparse = false;
        }
        defaults.sparseRandomMinValue = 0.1f;
        defaults.sparseRandomMaxValue = 0.95f;
    }

    // ===== GRADIENT FIELD =====
    if (modeType == InitialConditionType::GradientField) {
        defaults.gradientFieldScale = 1.0f + 0.3f * (complexity / 2.0f);
        defaults.gradientFieldPerturbation = std::clamp(0.05f + 0.1f * complexity, 0.0f, 0.3f);
    }

    // ===== CHECKERBOARD =====
    if (modeType == InitialConditionType::Checkerboard) {
        if (cellVarCount > 2) {
            defaults.checkerboardCellSize = 2;
        } else if (cellVarCount > 1) {
            defaults.checkerboardCellSize = 3;
        } else {
            defaults.checkerboardCellSize = 4;
        }
        defaults.checkerboardDarkValue = 0.2f;
        defaults.checkerboardLightValue = 0.8f;
    }

    // ===== RADIAL PATTERN =====
    if (modeType == InitialConditionType::RadialPattern) {
        defaults.radialPatternRingCount = std::clamp(3 + cellVarCount, 3, 12);
        defaults.radialPatternFalloff = 0.8f + 0.3f * (complexity / 2.0f);
    }

    // ===== MULTISCALE =====
    if (modeType == InitialConditionType::MultiScale) {
        defaults.multiScaleScaleCount = std::clamp(2 + cellVarCount / 2, 2, 6);
        defaults.multiScaleBaseFrequency = 0.5f;
        defaults.multiScaleFrequencyScale = 1.8f + 0.2f * complexity;
        defaults.multiScaleAmplitudeScale = 0.6f;
    }

    // ===== DIFFUSION LIMIT =====
    if (modeType == InitialConditionType::DiffusionLimit) {
        defaults.diffusionLimitSeedCount = std::clamp(3 + cellVarCount, 2, 12);
        defaults.diffusionLimitGrowthRate = 0.1f + 0.15f * (complexity / 2.0f);
        defaults.diffusionLimitColorVariance = 0.2f + 0.2f * complexity;
    }

    return defaults;
}

std::vector<InitialConditionType> GenerationAdvisor::viableGenerationModes(
    const initialization::ModelVariableCatalog& catalog) {
    std::vector<InitialConditionType> viable;

    if (catalog.variables.empty()) {
        viable.push_back(InitialConditionType::Blank);
        return viable;
    }

    const int cellVarCount = static_cast<int>(catalog.cellVariableIds().size());

    if (!catalog.supportedInitializationModes.empty()) {
        for (const InitialConditionType mode : catalog.supportedInitializationModes) {
            if (modeRequiresCellVariables(mode) && cellVarCount == 0) {
                continue;
            }
            if (!containsMode(viable, mode)) {
                viable.push_back(mode);
            }
        }
        if (viable.empty()) {
            viable.push_back(InitialConditionType::Blank);
        }
        return viable;
    }

    viable.push_back(InitialConditionType::Blank);
    if (cellVarCount == 0) {
        return viable;
    }

    // All spatial modes are viable with at least one cell variable
    viable.push_back(InitialConditionType::Terrain);
    viable.push_back(InitialConditionType::Conway);
    viable.push_back(InitialConditionType::GrayScott);
    viable.push_back(InitialConditionType::Waves);
    viable.push_back(InitialConditionType::Voronoi);
    viable.push_back(InitialConditionType::Clustering);
    viable.push_back(InitialConditionType::SparseRandom);
    viable.push_back(InitialConditionType::GradientField);
    viable.push_back(InitialConditionType::Checkerboard);
    viable.push_back(InitialConditionType::RadialPattern);

    if (cellVarCount >= 2) {
        viable.push_back(InitialConditionType::MultiScale);
        viable.push_back(InitialConditionType::DiffusionLimit);
    }

    if (catalog.preferredInitializationMode.has_value()) {
        const InitialConditionType preferredMode = *catalog.preferredInitializationMode;
        const bool preferredViable = !modeRequiresCellVariables(preferredMode) || (cellVarCount > 0);
        if (preferredViable && !containsMode(viable, preferredMode)) {
            viable.push_back(preferredMode);
        }

        if (preferredViable && !viable.empty() && viable.front() != preferredMode) {
            auto it = std::find(viable.begin(), viable.end(), preferredMode);
            if (it != viable.end()) {
                viable.erase(it);
                viable.insert(viable.begin(), preferredMode);
            }
        }
    }

    return viable;
}

std::string GenerationAdvisor::describeGenerationMode(InitialConditionType type) {
    switch (type) {
        case InitialConditionType::Terrain:
            return "Fractal terrain with islands, climate gradients, and hydrology. Best for spatial geographic models.";
        case InitialConditionType::Conway:
            return "Cellular automata seeding with Game of Life smoothing. Best for discrete state populations.";
        case InitialConditionType::GrayScott:
            return "Reaction-diffusion spots for pattern formation. Best for multi-component systems.";
        case InitialConditionType::Waves:
            return "Wave packet initialization with ripple patterns. Best for fluid and wave dynamics.";
        case InitialConditionType::Voronoi:
            return "Tessellation into Voronoi regions. Best for partitioned or clustered domains.";
        case InitialConditionType::Clustering:
            return "Radial cluster growth with decay falloff. Best for localized concentration.";
        case InitialConditionType::SparseRandom:
            return "Probabilistic sparse seeding with optional clustering. Best for dilute systems.";
        case InitialConditionType::GradientField:
            return "Smooth spatial gradients with optional perturbation. Best for directional fields.";
        case InitialConditionType::Checkerboard:
            return "Regular geometric checkerboard or diagonal patterns. Best for symmetric test cases.";
        case InitialConditionType::RadialPattern:
            return "Concentric rings radiating from center. Best for radial or symmetric dynamics.";
        case InitialConditionType::MultiScale:
            return "Multi-octave noise blending for hierarchical structure. Best for complex fields.";
        case InitialConditionType::DiffusionLimit:
            return "Random-walk-limited growth aggregation. Best for fractal or branching patterns.";
        case InitialConditionType::Blank:
            return "Zero initialization. Use for custom manual setup or data-driven initialization.";
        default:
            return "Unknown mode.";
    }
}

} // namespace ws::gui
