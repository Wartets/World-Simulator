#pragma once

// Private runtime initialization configuration types.
// These types define parameters for various initial condition generators.
// They are kept in a detail header to reduce the public API surface while
// maintaining all functionality. The public API only exposes RuntimeConfig
// which selects one InitialConditionType and uses its parameters.

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <optional>

namespace ws::detail {

// Types of initial condition generators (kept private to reduce public API).
// These are only used internally by RuntimeConfig.
enum class InitialConditionType : std::uint8_t {
    Terrain = 0,
    Conway = 1,
    GrayScott = 2,
    Waves = 3,
    Blank = 4,
    Voronoi = 5,
    Clustering = 6,
    SparseRandom = 7,
    GradientField = 8,
    Checkerboard = 9,
    RadialPattern = 10,
    MultiScale = 11,
    DiffusionLimit = 12
};

// Terrain-based initial condition parameters.
struct TerrainParams {
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
};

// Conway's Game of Life parameters.
struct ConwayParams {
    std::string targetVariable = "initialization.conway.target";
    float aliveProbability = 0.5f;
    float aliveValue = 1.0f;
    float deadValue = 0.0f;
    int smoothingPasses = 0;
};

// Gray-Scott reaction-diffusion parameters.
struct GrayScottParams {
    std::string targetVariableA = "initialization.gray_scott.target_a";
    std::string targetVariableB = "initialization.gray_scott.target_b";
    float backgroundA = 1.0f;
    float backgroundB = 0.0f;
    float spotValueA = 0.0f;
    float spotValueB = 1.0f;
    int spotCount = 4;
    float spotRadius = 15.0f;
    float spotJitter = 0.35f;
};

// Wave propagation parameters.
struct WaveParams {
    std::string targetVariable = "initialization.waves.target";
    float baseline = 0.0f;
    float dropAmplitude = 1.0f;
    float dropRadius = 5.0f;
    int dropCount = 1;
    float dropJitter = 0.35f;
    float ringFrequency = 1.0f;
};

// Voronoi tessellation parameters.
struct VoronoiParams {
    std::string targetVariable = "initialization.voronoi.target";
    int seedCount = 12;
    float smoothing = 0.3f;
    float colorScale = 1.0f;
    float jitter = 0.5f;
};

// Clustering initialization parameters.
struct ClusteringParams {
    std::string targetVariable = "initialization.clustering.target";
    int clusterCount = 8;
    float clusterRadius = 20.0f;
    float clusterIntensity = 0.8f;
    float clusterDecay = 0.6f;
    float clusterSpread = 0.4f;
};

// Sparse random initialization parameters.
struct SparseRandomParams {
    std::string targetVariable = "initialization.sparse_random.target";
    float fillFraction = 0.1f;
    float clusterRadius = 5.0f;
    float valueRange = 1.0f;
};

// Gradient field initialization parameters.
struct GradientFieldParams {
    std::string targetVariable = "initialization.gradient.target";
    int direction = 0;  // 0=horizontal, 1=vertical, 2=diagonal, 3=radial
    float startValue = 0.0f;
    float endValue = 1.0f;
    float smoothness = 0.0f;
};

// Checkerboard initialization parameters.
struct CheckerboardParams {
    std::string targetVariable = "initialization.checkerboard.target";
    int squareSize = 4;
    float value1 = 0.0f;
    float value2 = 1.0f;
};

// Radial pattern parameters.
struct RadialPatternParams {
    std::string targetVariable = "initialization.radial.target";
    int ringCount = 5;
    float centerValue = 1.0f;
    float outerValue = 0.7f;
    float falloff = 1.0f;
};

// Multi-scale pattern parameters.
struct MultiScaleParams {
    std::string targetVariable = "initialization.multiscale.target";
    int scaleCount = 3;
    float baseFrequency = 0.5f;
    float frequencyScale = 2.0f;
    float amplitudeScale = 0.5f;
    float blendMode = 0.5f;
};

// Diffusion-limited aggregation parameters.
struct DiffusionLimitParams {
    std::string targetVariable = "initialization.diffusion.target";
    int seedCount = 5;
    float growthRate = 0.2f;
    float anisotropy = 0.0f;
    float colorVariance = 0.3f;
    float randomWalk = 0.15f;
};

// Complete initial condition configuration (kept private).
// Clients should use the public RuntimeConfig instead.
struct InitialConditionConfig {
    InitialConditionType type = InitialConditionType::Terrain;
    TerrainParams terrain;
    ConwayParams conway;
    GrayScottParams grayScott;
    WaveParams waves;
    VoronoiParams voronoi;
    ClusteringParams clustering;
    SparseRandomParams sparseRandom;
    GradientFieldParams gradientField;
    CheckerboardParams checkerboard;
    RadialPatternParams radialPattern;
    MultiScaleParams multiScale;
    DiffusionLimitParams diffusionLimit;
};

} // namespace ws::detail

