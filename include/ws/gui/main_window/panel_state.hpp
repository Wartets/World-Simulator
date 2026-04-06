#pragma once

#include <cstdint>

namespace ws::gui::main_window {

// =============================================================================
// Panel State
// =============================================================================

// State variables for the main window panel UI.
struct PanelState {
    // Seed and Grid Configuration
    std::uint64_t seed = 42;           // Random seed for simulation.
    bool useManualSeed = false;        // Use manual seed instead of default.
    int gridWidth = 128;               // Grid width in cells.
    int gridHeight = 128;              // Grid height in cells.
    int tierIndex = 0;                  // Model tier selection index.
    int temporalIndex = 0;              // Temporal policy selection index.

    // Stepping Configuration
    int stepCount = 1;                 // Number of steps per frame.
    std::uint64_t runUntilTarget = 100; // Target step for run-until feature.
    bool showAdvancedStepping = false;  // Show advanced stepping options.
    float playbackSpeed = 1.0f;        // Playback speed multiplier.
    bool playbackSpeedDirty = false;   // Playback speed needs update.
    std::uint64_t seekTargetStep = 0;   // Target step for seeking.
    int backwardStepCount = 1;         // Steps to step backward.
    int checkpointIntervalSteps = 100;   // Interval between checkpoints.
    int checkpointRetentionCount = 64;  // Maximum checkpoints to retain.

    // Physics Parameters
    float forceFieldScale = 1.0f;      // Force field magnitude.
    float forceFieldDamping = 0.15f;    // Force field damping.
    float particleMobility = 0.6f;      // Particle mobility factor.
    float particleCohesion = 0.2f;      // Particle cohesion factor.
    float constraintRigidity = 0.5f;    // Constraint rigidity.
    float constraintTolerance = 0.1f;  // Constraint tolerance.

    // Terrain Generation Parameters
    float terrainBaseFrequency = 2.2f;     // Base frequency of terrain noise.
    float terrainDetailFrequency = 7.5f;   // Detail frequency of terrain.
    float terrainWarpStrength = 0.55f;      // Warping strength.
    float terrainAmplitude = 1.0f;         // Terrain amplitude.
    float terrainRidgeMix = 0.28f;         // Ridge noise mixing.
    int terrainOctaves = 5;                // Number of noise octaves.
    float terrainLacunarity = 2.0f;        // Lacunarity factor.
    float terrainGain = 0.5f;              // Gain factor.
    float seaLevel = 0.48f;               // Sea level threshold.
    float polarCooling = 0.62f;            // Polar cooling effect.
    float latitudeBanding = 1.0f;          // Latitude banding strength.
    float humidityFromWater = 0.52f;       // Humidity from water proximity.
    float biomeNoiseStrength = 0.20f;      // Biome noise strength.
    float islandDensity = 0.58f;           // Island density.
    float islandFalloff = 1.35f;           // Island falloff rate.
    float coastlineSharpness = 1.10f;     // Coastline sharpness.
    float archipelagoJitter = 0.85f;      // Archipelago jitter.
    float erosionStrength = 0.32f;         // Erosion strength.
    float shelfDepth = 0.20f;              // Continental shelf depth.

    // Initial Condition Parameters
    int initialConditionTypeIndex = 0;    // Selected initial condition type.
    char conwayTargetVariable[128] = "";   // Target variable for Conway.
    float conwayAliveProbability = 0.5f;   // Alive cell probability.
    float conwayAliveValue = 1.0f;          // Value for alive cells.
    float conwayDeadValue = 0.0f;           // Value for dead cells.
    int conwaySmoothingPasses = 0;         // Smoothing passes.
    char grayScottTargetVariableA[128] = "";  // Gray-Scott variable A.
    char grayScottTargetVariableB[128] = "";  // Gray-Scott variable B.
    float grayScottBackgroundA = 1.0f;     // Background value for A.
    float grayScottBackgroundB = 0.0f;      // Background value for B.
    float grayScottSpotValueA = 0.0f;        // Spot value for A.
    float grayScottSpotValueB = 1.0f;        // Spot value for B.
    int grayScottSpotCount = 4;             // Number of spots.
    float grayScottSpotRadius = 15.0f;     // Spot radius.
    float grayScottSpotJitter = 0.35f;      // Spot position jitter.
    char wavesTargetVariable[128] = "";    // Target variable for waves.
    float waveBaseline = 0.0f;              // Baseline wave value.
    float waveDropAmplitude = 1.0f;         // Drop amplitude.
    float waveDropRadius = 5.0f;           // Drop radius.
    int waveDropCount = 1;                  // Number of drops.
    float waveDropJitter = 0.35f;          // Drop position jitter.
    float waveRingFrequency = 1.0f;         // Ring frequency.

    // Profile and Checkpoint
    char profileName[128] = "baseline";    // Active profile name.
    char summaryVariable[128] = "";        // Variable for summary display.
    char checkpointLabel[128] = "quick";   // Checkpoint label.

    // Parameter Controls
    char parameterPresetName[128] = "default_runtime";  // Parameter preset name.
    int selectedParameterIndex = 0;        // Selected parameter index.
    float parameterValue = 0.0f;           // Parameter value.
    bool parameterValueDirty = false;      // Parameter needs update.
    char selectedParameterName[128] = "";  // Selected parameter name.

    // Manual Patching
    char manualPatchVariable[128] = "";   // Variable to patch.
    bool manualPatchGlobal = false;         // Apply globally.
    int manualPatchX = 0;                   // Patch X coordinate.
    int manualPatchY = 0;                  // Patch Y coordinate.
    float manualPatchValue = 0.0f;          // Patch value.
    char manualPatchNote[160] = "";        // Patch note.

    // Perturbation
    int perturbationTypeIndex = 0;          // Perturbation type index.
    char perturbationVariable[128] = "";   // Variable to perturb.
    float perturbationAmplitude = 0.05f;   // Perturbation amplitude.
    int perturbationStartStepOffset = 0;    // Start step offset.
    int perturbationDuration = 1;          // Duration in steps.
    int perturbationOriginX = 0;           // Origin X coordinate.
    int perturbationOriginY = 0;            // Origin Y coordinate.
    int perturbationWidth = 8;              // Perturbation width.
    int perturbationHeight = 8;            // Perturbation height.
    float perturbationSigma = 3.0f;         // Gaussian sigma.
    float perturbationFrequency = 0.2f;      // Perturbation frequency.
    std::uint64_t perturbationNoiseSeed = 1;  // Noise seed.
    char perturbationNote[160] = "";       // Perturbation note.

    // Event Logging
    char eventLogFileName[128] = "manual_events.json";  // Event log filename.
};

} // namespace ws::gui::main_window
