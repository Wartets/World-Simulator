#pragma once

#include "ws/core/initialization_binding.hpp"
#include "ws/core/runtime.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ws::gui {

// Recommendation for initial condition generation mode.
struct GenerationModeRecommendation {
    InitialConditionType recommendedType;
    float confidence = 0.0f;
    std::string rationale;
};

// Default parameter values for various generation modes.
struct GenerationParameterDefaults {
    // Terrain parameters
    float terrainBaseFrequency = 1.5f;
    float terrainDetailFrequency = 6.0f;
    float terrainWarpStrength = 0.8f;
    float terrainAmplitude = 1.0f;
    float terrainRidgeMix = 0.5f;
    int terrainOctaves = 4;
    float terrainLacunarity = 2.0f;
    float terrainGain = 0.5f;
    float seaLevel = 0.4f;
    float polarCooling = 0.6f;
    float latitudeBanding = 0.8f;
    float humidityFromWater = 0.7f;
    float biomeNoiseStrength = 0.5f;
    float islandDensity = 0.4f;
    float islandFalloff = 1.2f;
    float coastlineSharpness = 1.5f;
    float archipelagoJitter = 0.4f;
    float erosionStrength = 0.4f;
    float shelfDepth = 0.3f;

    // Conway parameters
    float conwayAliveProbability = 0.45f;
    float conwayAliveValue = 1.0f;
    float conwayDeadValue = 0.0f;
    int conwaySmoothingPasses = 2;

    // Gray-Scott parameters
    float grayScottBackgroundA = 0.5f;
    float grayScottBackgroundB = 0.25f;
    float grayScottSpotValueA = 1.0f;
    float grayScottSpotValueB = 0.0f;
    int grayScottSpotCount = 8;
    float grayScottSpotRadius = 15.0f;
    float grayScottSpotJitter = 0.3f;

    // Waves parameters
    float waveBaseline = 0.5f;
    float waveDropAmplitude = 0.8f;
    float waveDropRadius = 30.0f;
    int waveDropCount = 3;
    float waveDropJitter = 0.5f;
    float waveRingFrequency = 2.0f;

    // Voronoi parameters
    int voronoiSeedCount = 12;
    float voronoiSmoothing = 0.3f;
    float voronoiColorScale = 1.0f;
    float voronoiJitter = 0.5f;

    // Clustering parameters
    int clusteringClusterCount = 8;
    float clusteringClusterRadius = 20.0f;
    float clusteringClusterIntensity = 0.8f;
    float clusteringClusterDecay = 0.6f;
    float clusteringClusterSpread = 0.4f;

    // Sparse Random parameters
    float sparseRandomFillDensity = 0.15f;
    float sparseRandomMinValue = 0.2f;
    float sparseRandomMaxValue = 0.9f;
    bool sparseRandomClusterSparse = false;
    float sparseRandomClusterRadius = 8.0f;

    // Gradient Field parameters
    int gradientFieldDirectionMode = 0;
    float gradientFieldScale = 1.0f;
    float gradientFieldCenterX = 0.5f;
    float gradientFieldCenterY = 0.5f;
    float gradientFieldPerturbation = 0.1f;

    // Checkerboard parameters
    int checkerboardCellSize = 4;
    float checkerboardDarkValue = 0.2f;
    float checkerboardLightValue = 0.8f;
    float checkerboardBlurRadius = 0.0f;
    bool checkerboardDiagonal = false;

    // Radial Pattern parameters
    float radialPatternCenterX = 0.5f;
    float radialPatternCenterY = 0.5f;
    int radialPatternRingCount = 6;
    float radialPatternInnerValue = 0.3f;
    float radialPatternOuterValue = 0.7f;
    float radialPatternFalloff = 1.0f;

    // MultiScale parameters
    int multiScaleScaleCount = 3;
    float multiScaleBaseFrequency = 0.5f;
    float multiScaleFrequencyScale = 2.0f;
    float multiScaleAmplitudeScale = 0.5f;
    float multiScaleBlendMode = 0.5f;

    // Diffusion Limit parameters
    int diffusionLimitSeedCount = 5;
    float diffusionLimitGrowthRate = 0.2f;
    float diffusionLimitAnisotropy = 0.0f;
    float diffusionLimitColorVariance = 0.3f;
    float diffusionLimitRandomWalk = 0.15f;
};

// Provides recommendations for initial condition generation based on model analysis.
class GenerationAdvisor {
public:
    // Analyzes model catalog and parameters to recommend best generation mode.
    static GenerationModeRecommendation recommendGenerationMode(
        const initialization::ModelVariableCatalog& catalog,
        const std::vector<ParameterControl>& parameters);

    // Returns default parameters for a given generation mode.
    static GenerationParameterDefaults recommendDefaultParameters(
        const initialization::ModelVariableCatalog& catalog,
        InitialConditionType modeType);

    // Lists all viable generation modes for the given model.
    static std::vector<InitialConditionType> viableGenerationModes(
        const initialization::ModelVariableCatalog& catalog);

    // Returns human-readable description of a generation mode.
    static std::string describeGenerationMode(InitialConditionType type);

private:
    // Helper: checks if catalog has variables with specific tag.
    static bool catalogHasVariablesWithTag(
        const initialization::ModelVariableCatalog& catalog,
        const std::string& tag);

    // Helper: checks if catalog has variables with specific hint.
    static bool catalogHasVariablesWithHint(
        const initialization::ModelVariableCatalog& catalog,
        const std::string& hint);

    // Helper: counts variables with specific role.
    static int countVariablesWithRole(
        const initialization::ModelVariableCatalog& catalog,
        const std::string& role);

    // Helper: counts variables with specific tag.
    static int countVariablesWithTag(
        const initialization::ModelVariableCatalog& catalog,
        const std::string& tag);

    // Estimates computational complexity for model.
    static float estimateModelComplexity(
        const initialization::ModelVariableCatalog& catalog,
        const std::vector<ParameterControl>& parameters);
};

} // namespace ws::gui
