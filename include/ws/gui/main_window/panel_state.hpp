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

} // namespace ws::gui::main_window
