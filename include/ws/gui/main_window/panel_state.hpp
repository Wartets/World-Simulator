#pragma once

#include <cstdint>

namespace ws::gui::main_window {

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
    float playbackSpeed = 1.0f;
    std::uint64_t seekTargetStep = 0;
    int backwardStepCount = 1;
    int checkpointIntervalSteps = 100;
    int checkpointRetentionCount = 64;

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

    int initialConditionTypeIndex = 0;
    char conwayTargetVariable[128] = "vegetation_v";
    float conwayAliveProbability = 0.5f;
    float conwayAliveValue = 1.0f;
    float conwayDeadValue = 0.0f;
    int conwaySmoothingPasses = 0;
    char grayScottTargetVariableA[128] = "resource_stock_r";
    char grayScottTargetVariableB[128] = "vegetation_v";
    float grayScottBackgroundA = 1.0f;
    float grayScottBackgroundB = 0.0f;
    float grayScottSpotValueA = 0.0f;
    float grayScottSpotValueB = 1.0f;
    int grayScottSpotCount = 4;
    float grayScottSpotRadius = 15.0f;
    float grayScottSpotJitter = 0.35f;
    char wavesTargetVariable[128] = "surface_water_w";
    float waveBaseline = 0.0f;
    float waveDropAmplitude = 1.0f;
    float waveDropRadius = 5.0f;
    int waveDropCount = 1;
    float waveDropJitter = 0.35f;
    float waveRingFrequency = 1.0f;

    char profileName[128] = "baseline";
    char summaryVariable[128] = "";
    char checkpointLabel[128] = "quick";

    char parameterPresetName[128] = "default_runtime";
    int selectedParameterIndex = 0;
    float parameterValue = 0.0f;
    char manualPatchVariable[128] = "";
    bool manualPatchGlobal = false;
    int manualPatchX = 0;
    int manualPatchY = 0;
    float manualPatchValue = 0.0f;
    char manualPatchNote[160] = "";

    int perturbationTypeIndex = 0;
    char perturbationVariable[128] = "";
    float perturbationAmplitude = 0.05f;
    int perturbationStartStepOffset = 0;
    int perturbationDuration = 1;
    int perturbationOriginX = 0;
    int perturbationOriginY = 0;
    int perturbationWidth = 8;
    int perturbationHeight = 8;
    float perturbationSigma = 3.0f;
    float perturbationFrequency = 0.2f;
    std::uint64_t perturbationNoiseSeed = 1;
    char perturbationNote[160] = "";

    char eventLogFileName[128] = "manual_events.json";
};

} // namespace ws::gui::main_window
