#include "ws/core/initialization_strategy.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void allocateCanonicalFields(ws::StateStore& stateStore) {
    const std::vector<ws::VariableSpec> specs = {
        {0, "terrain_elevation_h"},
        {1, "surface_water_w"},
        {2, "temperature_T"},
        {3, "humidity_q"},
        {4, "wind_u"},
        {5, "climate_index_c"},
        {6, "fertility_phi"},
        {7, "vegetation_v"},
        {8, "resource_stock_r"},
        {9, "event_signal_e"},
        {10, "event_water_delta"},
        {11, "event_temperature_delta"},
        {12, "bootstrap_marker"},
        {13, "seed_probe"},
        {14, "wind_v"}
    };

    for (const auto& spec : specs) {
        if (!stateStore.hasField(spec.name)) {
            stateStore.allocateScalarField(spec);
        }
    }
}

float sample(const ws::StateStore& stateStore, const std::string& field, const std::uint32_t x, const std::uint32_t y) {
    return stateStore.trySampleScalar(field, ws::CellSigned{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)}).value_or(0.0f);
}

void verifyConwayDeterminism() {
    ws::RuntimeConfig config;
    config.seed = 42;
    config.grid = ws::GridSpec{16, 16};
    config.initialConditions.type = ws::InitialConditionType::Conway;
    config.initialConditions.conway.targetVariable = "vegetation_v";
    config.initialConditions.conway.aliveProbability = 0.4f;
    config.initialConditions.conway.aliveValue = 1.0f;
    config.initialConditions.conway.deadValue = 0.0f;
    config.initialConditions.conway.smoothingPasses = 2;

    ws::StateStore first(config.grid);
    ws::StateStore second(config.grid);
    allocateCanonicalFields(first);
    allocateCanonicalFields(second);

    ws::initialization::applyNonTerrainInitialization(first, config);
    ws::initialization::applyNonTerrainInitialization(second, config);

    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            assert(sample(first, "vegetation_v", x, y) == sample(second, "vegetation_v", x, y));
        }
    }
}

void verifyGrayScottWritesBothTargets() {
    ws::RuntimeConfig config;
    config.seed = 27182;
    config.grid = ws::GridSpec{24, 24};
    config.initialConditions.type = ws::InitialConditionType::GrayScott;
    config.initialConditions.grayScott.targetVariableA = "resource_stock_r";
    config.initialConditions.grayScott.targetVariableB = "vegetation_v";
    config.initialConditions.grayScott.backgroundA = 1.0f;
    config.initialConditions.grayScott.backgroundB = 0.0f;
    config.initialConditions.grayScott.spotValueA = 0.0f;
    config.initialConditions.grayScott.spotValueB = 1.0f;
    config.initialConditions.grayScott.spotCount = 4;
    config.initialConditions.grayScott.spotRadius = 6.0f;
    config.initialConditions.grayScott.spotJitter = 0.35f;

    ws::StateStore store(config.grid);
    allocateCanonicalFields(store);

    ws::initialization::applyNonTerrainInitialization(store, config);

    bool foundSpotA = false;
    bool foundSpotB = false;
    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            if (sample(store, "resource_stock_r", x, y) == config.initialConditions.grayScott.spotValueA) {
                foundSpotA = true;
            }
            if (sample(store, "vegetation_v", x, y) == config.initialConditions.grayScott.spotValueB) {
                foundSpotB = true;
            }
        }
    }

    assert(foundSpotA);
    assert(foundSpotB);
}

void verifyWavesCreatesSignal() {
    ws::RuntimeConfig config;
    config.seed = 16180;
    config.grid = ws::GridSpec{32, 32};
    config.initialConditions.type = ws::InitialConditionType::Waves;
    config.initialConditions.waves.targetVariable = "surface_water_w";
    config.initialConditions.waves.baseline = 0.2f;
    config.initialConditions.waves.dropAmplitude = 1.0f;
    config.initialConditions.waves.dropRadius = 8.0f;
    config.initialConditions.waves.dropCount = 3;
    config.initialConditions.waves.dropJitter = 0.4f;
    config.initialConditions.waves.ringFrequency = 1.4f;

    ws::StateStore store(config.grid);
    allocateCanonicalFields(store);

    ws::initialization::applyNonTerrainInitialization(store, config);

    float maxValue = -1e9f;
    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            maxValue = std::max(maxValue, sample(store, "surface_water_w", x, y));
        }
    }

    assert(maxValue > config.initialConditions.waves.baseline);
}

void verifyTerrainRejected() {
    ws::RuntimeConfig config;
    config.grid = ws::GridSpec{8, 8};
    config.initialConditions.type = ws::InitialConditionType::Terrain;

    ws::StateStore store(config.grid);
    allocateCanonicalFields(store);

    bool threw = false;
    try {
        ws::initialization::applyNonTerrainInitialization(store, config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    verifyConwayDeterminism();
    verifyGrayScottWritesBothTargets();
    verifyWavesCreatesSignal();
    verifyTerrainRejected();
    return 0;
}
