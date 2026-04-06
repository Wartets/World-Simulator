#include "ws/core/subsystems/subsystems.hpp"
#include "ws/core/field_resolver.hpp"
#include "ws/core/determinism.hpp"
#include "ws/core/openmp_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ws {

namespace {

// Clamps a value to the specified range.
float clampRange(const float value, const float low, const float high) {
    return std::clamp(value, low, high);
}

// 64-bit hash mixing function (MurmurHash3 finalizer).
std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

// Generates a deterministic hash value in [0,1] from seed and coordinates.
float hash01(const std::uint64_t seed, const int x, const int y) {
    std::uint64_t h = DeterministicHash::combine(seed, DeterministicHash::hashPod(x));
    h = DeterministicHash::combine(h, DeterministicHash::hashPod(y));
    h = mix64(h);
    const std::uint32_t top24 = static_cast<std::uint32_t>((h >> 40u) & 0xFFFFFFu);
    return static_cast<float>(top24) / static_cast<float>(0xFFFFFFu);
}

StateStore::FieldHandle resolveReadHandle(
    const StateStore& stateStore,
    const std::string& semanticKey,
    const std::string& subsystemName) {
    return FieldResolver::resolveRequiredFieldHandle(stateStore, semanticKey, subsystemName);
}

StateStore::FieldHandle resolveWriteHandle(
    const StateStore& stateStore,
    StateStore::WriteSession& writeSession,
    const std::string& semanticKey,
    const std::string& subsystemName) {
    const auto variableName = FieldResolver::resolveRequiredField(stateStore, semanticKey, subsystemName);
    const auto handle = writeSession.getFieldHandle(variableName);
    if (handle == StateStore::InvalidHandle) {
        throw std::runtime_error(
            subsystemName + ": variable '" + variableName + "' is not writable in current session");
    }
    return handle;
}

} // namespace

std::string CellularAutomatonSubsystem::name() const { return "automaton"; }
std::vector<std::string> CellularAutomatonSubsystem::declaredReadSet() const { return {"automaton.state"}; }
std::vector<std::string> CellularAutomatonSubsystem::declaredWriteSet() const { return {"automaton.state"}; }

void CellularAutomatonSubsystem::initialize(const StateStore&, StateStore::WriteSession&, const ModelProfile&) {}

void CellularAutomatonSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile&, const std::uint64_t) {
    const auto readHandle = resolveReadHandle(stateStore, "automaton.state", name());
    const auto writeHandle = resolveWriteHandle(stateStore, writeSession, "automaton.state", name());
    const auto variableName = FieldResolver::resolveRequiredField(stateStore, "automaton.state", name());

    const GridSpec& grid = stateStore.grid();
    const float* currentPtr = stateStore.scalarFieldRawPtr(readHandle);
    float* writePtr = stateStore.scalarFieldRawPtrMut(writeHandle);
    const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
    std::vector<float> current(currentPtr, currentPtr + static_cast<std::ptrdiff_t>(logicalCount));

    const auto sampleAlive = [&](const std::int64_t x, const std::int64_t y) -> int {
        const Cell resolved = stateStore.resolveBoundary(CellSigned{x, y});
        const std::size_t idx = static_cast<std::size_t>(resolved.y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(resolved.x);
        return current[idx] >= 0.5f ? 1 : 0;
    };

    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width);
        for (std::int64_t x = 0; x < width; ++x) {
            int neighbors = 0;
            neighbors += sampleAlive(x - 1, y - 1);
            neighbors += sampleAlive(x, y - 1);
            neighbors += sampleAlive(x + 1, y - 1);
            neighbors += sampleAlive(x - 1, y);
            neighbors += sampleAlive(x + 1, y);
            neighbors += sampleAlive(x - 1, y + 1);
            neighbors += sampleAlive(x, y + 1);
            neighbors += sampleAlive(x + 1, y + 1);

            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const bool alive = current[idx] >= 0.5f;
            const bool nextAlive = (neighbors == 3) || (alive && neighbors == 2);
            writePtr[idx] = nextAlive ? 1.0f : 0.0f;
        }
    }
}

std::string ForestFireSubsystem::name() const { return "fire_spread"; }
std::vector<std::string> ForestFireSubsystem::declaredReadSet() const {
    return {"fire.state", "fire.vegetation", "fire.moisture", "fire.wind_factor"};
}
std::vector<std::string> ForestFireSubsystem::declaredWriteSet() const {
    return {"fire.state", "fire.vegetation", "fire.burning_neighbors"};
}

void ForestFireSubsystem::initialize(const StateStore&, StateStore::WriteSession&, const ModelProfile&) {}

void ForestFireSubsystem::step(
    const StateStore& stateStore,
    StateStore::WriteSession& writeSession,
    const ModelProfile&,
    const std::uint64_t stepIndex) {
    const auto fireReadHandle = resolveReadHandle(stateStore, "fire.state", name());
    const auto vegetationReadHandle = resolveReadHandle(stateStore, "fire.vegetation", name());
    const auto moistureReadHandle = resolveReadHandle(stateStore, "fire.moisture", name());
    const auto windFactorReadHandle = resolveReadHandle(stateStore, "fire.wind_factor", name());
    const auto fireWriteHandle = resolveWriteHandle(stateStore, writeSession, "fire.state", name());
    const auto vegetationWriteHandle = resolveWriteHandle(stateStore, writeSession, "fire.vegetation", name());
    const auto neighborsWriteHandle = resolveWriteHandle(stateStore, writeSession, "fire.burning_neighbors", name());

    const GridSpec& grid = stateStore.grid();
    const std::size_t logicalCount = static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
    const float* fireReadPtr = stateStore.scalarFieldRawPtr(fireReadHandle);
    const float* vegetationReadPtr = stateStore.scalarFieldRawPtr(vegetationReadHandle);
    const float* moistureReadPtr = stateStore.scalarFieldRawPtr(moistureReadHandle);
    const float* windFactorReadPtr = stateStore.scalarFieldRawPtr(windFactorReadHandle);
    float* fireWritePtr = stateStore.scalarFieldRawPtrMut(fireWriteHandle);
    float* vegetationWritePtr = stateStore.scalarFieldRawPtrMut(vegetationWriteHandle);
    float* neighborsWritePtr = stateStore.scalarFieldRawPtrMut(neighborsWriteHandle);

    std::vector<float> fireCurrent(fireReadPtr, fireReadPtr + static_cast<std::ptrdiff_t>(logicalCount));
    std::vector<float> vegetationCurrent(vegetationReadPtr, vegetationReadPtr + static_cast<std::ptrdiff_t>(logicalCount));
    std::vector<float> moistureCurrent(moistureReadPtr, moistureReadPtr + static_cast<std::ptrdiff_t>(logicalCount));
    std::vector<float> windFactorCurrent(windFactorReadPtr, windFactorReadPtr + static_cast<std::ptrdiff_t>(logicalCount));

    const auto sampleBurning = [&](const std::int64_t x, const std::int64_t y) -> int {
        const Cell resolved = stateStore.resolveBoundary(CellSigned{x, y});
        const std::size_t idx = static_cast<std::size_t>(resolved.y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(resolved.x);
        return (fireCurrent[idx] >= 0.5f && fireCurrent[idx] < 1.5f) ? 1 : 0;
    };

    const std::uint64_t rngSeed = DeterministicHash::combine(stepIndex + 1u, 0xF17E5101ULL);
    const float baseSpread = 0.60f;
    const float burnRate = 0.25f;
    const float regrowthRate = 0.001f;
    const float spontaneousIgnition = 0.0001f;

    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width);
        for (std::int64_t x = 0; x < width; ++x) {
            int burningNeighbors = 0;
            burningNeighbors += sampleBurning(x - 1, y - 1);
            burningNeighbors += sampleBurning(x, y - 1);
            burningNeighbors += sampleBurning(x + 1, y - 1);
            burningNeighbors += sampleBurning(x - 1, y);
            burningNeighbors += sampleBurning(x + 1, y);
            burningNeighbors += sampleBurning(x - 1, y + 1);
            burningNeighbors += sampleBurning(x, y + 1);
            burningNeighbors += sampleBurning(x + 1, y + 1);

            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float moisture = clampRange(moistureCurrent[idx], 0.0f, 1.0f);
            float vegetation = clampRange(vegetationCurrent[idx], 0.0f, 1.0f);
            float fireState = fireCurrent[idx];
            float nextState = fireState;

            if (fireState >= 0.5f && fireState < 1.5f) {
                const float burnAmount = burnRate * (0.65f + 0.35f * (1.0f - moisture));
                vegetation = std::max(0.0f, vegetation - burnAmount);
                if (vegetation <= 0.02f) {
                    nextState = 2.0f;
                } else {
                    nextState = 1.0f;
                }
            } else if (fireState < 0.5f) {
                const float fuel = clampRange((vegetation - 0.08f) / 0.92f, 0.0f, 1.0f);
                const float dryness = 1.0f - moisture;
                const float neighborhoodInfluence = static_cast<float>(burningNeighbors) / 8.0f;
                const float localWindFactor = clampRange(windFactorCurrent[idx], 0.0f, 1.0f);
                const float windMultiplier = 1.0f + 0.5f * localWindFactor;
                const float spreadChance = clampRange(baseSpread * neighborhoodInfluence * fuel * dryness * windMultiplier, 0.0f, 1.0f);
                const float ignitionChance = clampRange(spontaneousIgnition * fuel * dryness, 0.0f, 1.0f);
                const float randomDraw = hash01(rngSeed, static_cast<int>(x), static_cast<int>(y));

                if ((burningNeighbors > 0 && randomDraw < spreadChance) || randomDraw < ignitionChance) {
                    nextState = 1.0f;
                } else {
                    nextState = 0.0f;
                }
            } else {
                vegetation = std::min(1.0f, vegetation + regrowthRate * (0.4f + 0.6f * moisture));
                nextState = (vegetation > 0.20f) ? 0.0f : 2.0f;
            }

            fireWritePtr[idx] = nextState;
            vegetationWritePtr[idx] = vegetation;
            neighborsWritePtr[idx] = static_cast<float>(burningNeighbors);
        }
    }
}

std::string GenerationSubsystem::name() const { return "generation"; }
std::vector<std::string> GenerationSubsystem::declaredReadSet() const { return {"generation.elevation"}; }
std::vector<std::string> GenerationSubsystem::declaredWriteSet() const { return {"generation.elevation"}; }

void GenerationSubsystem::initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)stateStore;
    (void)writeSession;
    (void)profile;
    // Terrain is produced by the runtime seed pipeline and must remain non-periodic.
    // Keep initialize() as a no-op to avoid flattening/overwriting seeded macro-zones.
}

void GenerationSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;

    const auto h_terrain = resolveReadHandle(stateStore, "generation.elevation", name());
    const auto w_terrain = resolveWriteHandle(stateStore, writeSession, "generation.elevation", name());
    const GridSpec& grid = stateStore.grid();
    const float* h_terrain_ptr = stateStore.scalarFieldRawPtr(h_terrain);
    float* w_terrain_ptr = stateStore.scalarFieldRawPtrMut(w_terrain);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);

    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float center = h_terrain_ptr[idx];
            const float left  = (x > 0) ? h_terrain_ptr[idx - 1] : center;
            const float right = (x + 1 < width) ? h_terrain_ptr[idx + 1] : center;
            const float up    = (y > 0) ? h_terrain_ptr[idx - W] : center;
            const float down  = (y + 1 < height) ? h_terrain_ptr[idx + W] : center;
            const float neighborhood = 0.25f * (left + right + up + down);

            const float eroded = clampRange(center + 0.02f * (neighborhood - center), 0.0f, 1.0f);
            w_terrain_ptr[idx] = eroded;
        }
    }
}

std::string HydrologySubsystem::name() const { return "hydrology"; }
std::vector<std::string> HydrologySubsystem::declaredReadSet() const { return {"hydrology.elevation", "hydrology.humidity", "hydrology.climate", "hydrology.event_water_delta", "hydrology.water", "hydrology.transport.axis_x", "hydrology.transport.axis_y"}; }
std::vector<std::string> HydrologySubsystem::declaredWriteSet() const { return {"hydrology.water"}; }

void HydrologySubsystem::initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)stateStore;
    (void)writeSession;
    (void)profile;
}

void HydrologySubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;
    double total = 0.0;
    
    const auto h_elevation = resolveReadHandle(stateStore, "hydrology.elevation", name());
    const auto h_humidity = resolveReadHandle(stateStore, "hydrology.humidity", name());
    const auto h_climate = resolveReadHandle(stateStore, "hydrology.climate", name());
    const auto h_water = resolveReadHandle(stateStore, "hydrology.water", name());
    const auto h_eventWater = resolveReadHandle(stateStore, "hydrology.event_water_delta", name());
    const auto h_wind = resolveReadHandle(stateStore, "hydrology.transport.axis_x", name());
    const auto h_wind_v = resolveReadHandle(stateStore, "hydrology.transport.axis_y", name());
    const auto w_water = resolveWriteHandle(stateStore, writeSession, "hydrology.water", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_elevation_ptr = stateStore.scalarFieldRawPtr(h_elevation);
    const float* h_humidity_ptr = stateStore.scalarFieldRawPtr(h_humidity);
    const float* h_climate_ptr = stateStore.scalarFieldRawPtr(h_climate);
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    const float* h_eventWater_ptr = stateStore.scalarFieldRawPtr(h_eventWater);
    const float* h_wind_ptr = stateStore.scalarFieldRawPtr(h_wind);
    const float* h_wind_v_ptr = stateStore.scalarFieldRawPtr(h_wind_v);
    float* w_water_ptr = stateStore.scalarFieldRawPtrMut(w_water);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR_REDUCTION_SUM(total)
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float elevation = h_elevation_ptr[idx];
            const float humidity = h_humidity_ptr[idx];
            const float climate = h_climate_ptr[idx];
            const float priorWater = h_water_ptr[idx];
            const float eventPulse = h_eventWater_ptr[idx];
            
            float next = priorWater + 0.015f * humidity - 0.010f * elevation + 0.003f * climate + eventPulse;
            
            const float left  = (x > 0) ? h_water_ptr[idx - 1] : priorWater;
            const float right = (x + 1 < width) ? h_water_ptr[idx + 1] : priorWater;
            const float up    = (y > 0) ? h_water_ptr[idx - W] : priorWater;
            const float down  = (y + 1 < height) ? h_water_ptr[idx + W] : priorWater;
            const float neighborAvg = 0.25f * (left + right + up + down);
            next += 0.08f * (neighborAvg - priorWater);

            const float wind = h_wind_ptr[idx];
            const float windV = h_wind_v_ptr[idx];
            next += 0.015f * clampRange(wind, -8.0f, 8.0f);
            next += 0.010f * clampRange(windV, -8.0f, 8.0f);
            
            next = clampRange(next, 0.0f, 2.0f);
            w_water_ptr[idx] = next;
            total += static_cast<double>(next);
        }
    }
    
    if (!std::isfinite(total) || total < 0.0) {
        throw std::runtime_error("HydrologySubsystem conservation check failed: invalid water mass");
    }
}

std::string TemperatureSubsystem::name() const { return "temperature"; }
std::vector<std::string> TemperatureSubsystem::declaredReadSet() const { return {"temperature.elevation", "temperature.current", "temperature.climate", "temperature.transport.axis_x", "temperature.event_delta", "temperature.humidity"}; }
std::vector<std::string> TemperatureSubsystem::declaredWriteSet() const { return {"temperature.current"}; }

void TemperatureSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)writeSession;
    (void)profile;
}

void TemperatureSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t stepIndex) {
    (void)profile;
    const float diurnal = static_cast<float>((stepIndex % 24u)) / 24.0f;
    
    // Check out Temperature Generation issue: "When displaying the temperature, it's the same everywhere."
    // Right now Temperature initializes to a flat field, and then adds diurnal shifts and climate index which might be initialized to flat as well. Wait, climate_index_c is flat to start with.
    // That's why temperature is totally flat to start! We should inject some variance based on terrain.
    // User requested "is the same everywhere" - actually, it just updates flatly as well if terrain isn't affecting it fast enough.
    // Terrain has elevation, we should add an elevation effect!
    const auto h_terrain = resolveReadHandle(stateStore, "temperature.elevation", name());

    const auto h_prior = resolveReadHandle(stateStore, "temperature.current", name());
    const auto h_climate = resolveReadHandle(stateStore, "temperature.climate", name());
    const auto h_wind = resolveReadHandle(stateStore, "temperature.transport.axis_x", name());
    const auto h_eventDelta = resolveReadHandle(stateStore, "temperature.event_delta", name());
    const auto h_humidity = resolveReadHandle(stateStore, "temperature.humidity", name());
    const auto w_temp = resolveWriteHandle(stateStore, writeSession, "temperature.current", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_terrain_ptr = stateStore.scalarFieldRawPtr(h_terrain);
    const float* h_prior_ptr = stateStore.scalarFieldRawPtr(h_prior);
    const float* h_climate_ptr = stateStore.scalarFieldRawPtr(h_climate);
    const float* h_wind_ptr = stateStore.scalarFieldRawPtr(h_wind);
    const float* h_eventDelta_ptr = stateStore.scalarFieldRawPtr(h_eventDelta);
    const float* h_humidity_ptr = stateStore.scalarFieldRawPtr(h_humidity);
    float* w_temp_ptr = stateStore.scalarFieldRawPtrMut(w_temp);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float prior = h_prior_ptr[idx];
            const float climate = h_climate_ptr[idx];
            const float wind = h_wind_ptr[idx];
            const float eventDelta = h_eventDelta_ptr[idx];
            
            // Add a small terrain lapse rate so temperature immediately shows a gradient matching terrain
            const float elevation = h_terrain_ptr[idx];
            const float terrainLapse = -5.0f * elevation;
            
            float next = prior + 0.05f * climate - 0.03f * std::fabs(wind) + eventDelta + 0.2f * (diurnal - 0.5f) + 0.01f * terrainLapse;
            const float left  = (x > 0) ? h_prior_ptr[idx - 1] : prior;
            const float right = (x + 1 < width) ? h_prior_ptr[idx + 1] : prior;
            const float up    = (y > 0) ? h_prior_ptr[idx - W] : prior;
            const float down  = (y + 1 < height) ? h_prior_ptr[idx + W] : prior;
            const float neighborAvg = 0.25f * (left + right + up + down);
            next += 0.06f * (neighborAvg - prior);

            const float humidity = h_humidity_ptr[idx];
            next += 0.08f * (humidity - 0.5f);
            
            next = clampRange(next, 220.0f, 340.0f);
            w_temp_ptr[idx] = next;
        }
    }
}

std::string HumiditySubsystem::name() const { return "humidity"; }
std::vector<std::string> HumiditySubsystem::declaredReadSet() const { return {"humidity.water", "humidity.temperature", "humidity.vegetation", "humidity.current", "humidity.climate"}; }
std::vector<std::string> HumiditySubsystem::declaredWriteSet() const { return {"humidity.current"}; }

void HumiditySubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)writeSession;
    (void)profile;
}

void HumiditySubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;
    
    const auto h_water = resolveReadHandle(stateStore, "humidity.water", name());
    const auto h_temp = resolveReadHandle(stateStore, "humidity.temperature", name());
    const auto h_veg = resolveReadHandle(stateStore, "humidity.vegetation", name());
    const auto h_hum = resolveReadHandle(stateStore, "humidity.current", name());
    const auto h_cli = resolveReadHandle(stateStore, "humidity.climate", name());
    const auto w_hum = resolveWriteHandle(stateStore, writeSession, "humidity.current", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_veg_ptr = stateStore.scalarFieldRawPtr(h_veg);
    const float* h_hum_ptr = stateStore.scalarFieldRawPtr(h_hum);
    const float* h_cli_ptr = stateStore.scalarFieldRawPtr(h_cli);
    float* w_hum_ptr = stateStore.scalarFieldRawPtrMut(w_hum);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float water = h_water_ptr[idx];
            const float temp = h_temp_ptr[idx];
            const float vegetation = h_veg_ptr[idx];
            
            const float tempStress = clampRange((temp - 285.15f) / 40.0f, -1.0f, 1.0f);
            float next = 0.40f + 0.22f * water - 0.12f * tempStress + 0.08f * vegetation;
            const float left = (x > 0) ? h_hum_ptr[idx - 1] : h_hum_ptr[idx];
            next += 0.03f * left;

            const float climate = h_cli_ptr[idx];
            next += 0.06f * climate;
            
            next = clampRange(next, 0.0f, 1.0f);
            w_hum_ptr[idx] = next;
        }
    }
}

std::string WindSubsystem::name() const { return "wind"; }
std::vector<std::string> WindSubsystem::declaredReadSet() const { return {"wind.temperature", "wind.elevation", "wind.humidity"}; }
std::vector<std::string> WindSubsystem::declaredWriteSet() const { return {"wind.vector.axis_x", "wind.vector.axis_y"}; }

void WindSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    (void)writeSession;
}

void WindSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;
    
    const auto h_temp = resolveReadHandle(stateStore, "wind.temperature", name());
    const auto h_terrain = resolveReadHandle(stateStore, "wind.elevation", name());
    const auto h_hum = resolveReadHandle(stateStore, "wind.humidity", name());
    const auto w_wind = resolveWriteHandle(stateStore, writeSession, "wind.vector.axis_x", name());
    const auto w_wind_v = resolveWriteHandle(stateStore, writeSession, "wind.vector.axis_y", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_terrain_ptr = stateStore.scalarFieldRawPtr(h_terrain);
    const float* h_hum_ptr = stateStore.scalarFieldRawPtr(h_hum);
    float* w_wind_ptr = stateStore.scalarFieldRawPtrMut(w_wind);
    float* w_wind_v_ptr = stateStore.scalarFieldRawPtrMut(w_wind_v);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float eastTemp = (x + 1 < width) ? h_temp_ptr[idx + 1] : h_temp_ptr[idx];
            const float westTemp = (x > 0) ? h_temp_ptr[idx - 1] : h_temp_ptr[idx];
            const float eastTerrain = (x + 1 < width) ? h_terrain_ptr[idx + 1] : h_terrain_ptr[idx];
            const float westTerrain = (x > 0) ? h_terrain_ptr[idx - 1] : h_terrain_ptr[idx];
            const float northTemp = (y > 0) ? h_temp_ptr[idx - W] : h_temp_ptr[idx];
            const float southTemp = (y + 1 < height) ? h_temp_ptr[idx + W] : h_temp_ptr[idx];
            const float northTerrain = (y > 0) ? h_terrain_ptr[idx - W] : h_terrain_ptr[idx];
            const float southTerrain = (y + 1 < height) ? h_terrain_ptr[idx + W] : h_terrain_ptr[idx];
            
            float next = 0.02f * (eastTemp - westTemp) - 0.08f * (eastTerrain - westTerrain) + 0.01f * (southTemp - northTemp);
            float nextV = 0.02f * (southTemp - northTemp) - 0.08f * (southTerrain - northTerrain) + 0.01f * (eastTemp - westTemp);
            const float southHumidity = (y + 1 < height) ? h_hum_ptr[idx + W] : h_hum_ptr[idx];
            const float northHumidity = (y > 0) ? h_hum_ptr[idx - W] : h_hum_ptr[idx];
            const float eastHumidity = (x + 1 < width) ? h_hum_ptr[idx + 1] : h_hum_ptr[idx];
            const float westHumidity = (x > 0) ? h_hum_ptr[idx - 1] : h_hum_ptr[idx];
            next += 0.08f * (southHumidity - northHumidity);
            nextV += 0.08f * (eastHumidity - westHumidity);
            
            w_wind_ptr[idx] = clampRange(next, -8.0f, 8.0f);
            w_wind_v_ptr[idx] = clampRange(nextV, -8.0f, 8.0f);
        }
    }
}

std::string ClimateSubsystem::name() const { return "climate"; }
std::vector<std::string> ClimateSubsystem::declaredReadSet() const { return {"climate.temperature", "climate.humidity", "climate.transport.axis_x", "climate.current", "climate.water"}; }
std::vector<std::string> ClimateSubsystem::declaredWriteSet() const { return {"climate.current"}; }

void ClimateSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    (void)writeSession;
}

void ClimateSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;

    const auto h_temp = resolveReadHandle(stateStore, "climate.temperature", name());
    const auto h_hum = resolveReadHandle(stateStore, "climate.humidity", name());
    const auto h_wind = resolveReadHandle(stateStore, "climate.transport.axis_x", name());
    const auto h_cli = resolveReadHandle(stateStore, "climate.current", name());
    const auto h_water = resolveReadHandle(stateStore, "climate.water", name());
    const auto w_cli = resolveWriteHandle(stateStore, writeSession, "climate.current", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_hum_ptr = stateStore.scalarFieldRawPtr(h_hum);
    const float* h_wind_ptr = stateStore.scalarFieldRawPtr(h_wind);
    const float* h_cli_ptr = stateStore.scalarFieldRawPtr(h_cli);
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    float* w_cli_ptr = stateStore.scalarFieldRawPtrMut(w_cli);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float temp = h_temp_ptr[idx];
            const float humidity = h_hum_ptr[idx];
            const float wind = h_wind_ptr[idx];
            
            const float thermalTerm = clampRange((temp - 285.15f) / 20.0f, -3.0f, 3.0f);
            const float left  = (x > 0) ? h_cli_ptr[idx - 1] : h_cli_ptr[idx];
            const float right = (x + 1 < width) ? h_cli_ptr[idx + 1] : h_cli_ptr[idx];
            const float up    = (y > 0) ? h_cli_ptr[idx - W] : h_cli_ptr[idx];
            const float down  = (y + 1 < height) ? h_cli_ptr[idx + W] : h_cli_ptr[idx];
            const float neighborhood = 0.25f * (left + right + up + down);
            float next = 0.50f * thermalTerm + 0.80f * (humidity - 0.5f) - 0.12f * std::fabs(wind);
            next = 0.85f * next + 0.15f * neighborhood;
            next += 0.12f * (h_water_ptr[idx] - 0.5f);
            
            w_cli_ptr[idx] = clampRange(next, -4.0f, 4.0f);
        }
    }
}

std::string SoilSubsystem::name() const { return "soil"; }
std::vector<std::string> SoilSubsystem::declaredReadSet() const { return {"soil.water", "soil.temperature", "soil.fertility", "soil.climate"}; }
std::vector<std::string> SoilSubsystem::declaredWriteSet() const { return {"soil.fertility"}; }

void SoilSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)writeSession;
    (void)profile;
}

void SoilSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;

    const auto h_water = resolveReadHandle(stateStore, "soil.water", name());
    const auto h_temp = resolveReadHandle(stateStore, "soil.temperature", name());
    const auto h_fert = resolveReadHandle(stateStore, "soil.fertility", name());
    const auto h_cli = resolveReadHandle(stateStore, "soil.climate", name());
    const auto w_fert = resolveWriteHandle(stateStore, writeSession, "soil.fertility", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_fert_ptr = stateStore.scalarFieldRawPtr(h_fert);
    const float* h_cli_ptr = stateStore.scalarFieldRawPtr(h_cli);
    float* w_fert_ptr = stateStore.scalarFieldRawPtrMut(w_fert);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float water = h_water_ptr[idx];
            const float temp = h_temp_ptr[idx];
            
            const float thermalSuitability = 1.0f - std::fabs((temp - 290.15f) / 35.0f);
            const float down = (y + 1 < height) ? h_fert_ptr[idx + W] : h_fert_ptr[idx];
            float next = 0.35f + 0.30f * water + 0.30f * clampRange(thermalSuitability, 0.0f, 1.0f);
            next += 0.05f * down;
            next -= 0.04f * std::fabs(h_cli_ptr[idx]);
            
            w_fert_ptr[idx] = clampRange(next, 0.0f, 1.0f);
        }
    }
}

std::string VegetationSubsystem::name() const { return "vegetation"; }
std::vector<std::string> VegetationSubsystem::declaredReadSet() const { return {"vegetation.fertility", "vegetation.humidity", "vegetation.temperature", "vegetation.resources", "vegetation.current", "vegetation.water"}; }
std::vector<std::string> VegetationSubsystem::declaredWriteSet() const { return {"vegetation.current"}; }

void VegetationSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    (void)writeSession;
}

void VegetationSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;

    const auto h_fert = resolveReadHandle(stateStore, "vegetation.fertility", name());
    const auto h_hum = resolveReadHandle(stateStore, "vegetation.humidity", name());
    const auto h_temp = resolveReadHandle(stateStore, "vegetation.temperature", name());
    const auto h_res = resolveReadHandle(stateStore, "vegetation.resources", name());
    const auto h_veg = resolveReadHandle(stateStore, "vegetation.current", name());
    const auto h_water = resolveReadHandle(stateStore, "vegetation.water", name());
    const auto w_veg = resolveWriteHandle(stateStore, writeSession, "vegetation.current", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_fert_ptr = stateStore.scalarFieldRawPtr(h_fert);
    const float* h_hum_ptr = stateStore.scalarFieldRawPtr(h_hum);
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_res_ptr = stateStore.scalarFieldRawPtr(h_res);
    const float* h_veg_ptr = stateStore.scalarFieldRawPtr(h_veg);
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    float* w_veg_ptr = stateStore.scalarFieldRawPtrMut(w_veg);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float fertility = h_fert_ptr[idx];
            const float humidity = h_hum_ptr[idx];
            const float temperature = h_temp_ptr[idx];
            const float resources = h_res_ptr[idx];
            
            const float thermalSuitability = 1.0f - std::fabs((temperature - 289.15f) / 40.0f);
            const float east = (x + 1 < width) ? h_veg_ptr[idx + 1] : h_veg_ptr[idx];
            const float water = h_water_ptr[idx];
            float growth = 0.20f * fertility + 0.20f * humidity + 0.10f * resources + 0.25f * clampRange(thermalSuitability, 0.0f, 1.0f);
            growth += 0.08f * east;
            growth += 0.12f * water;
            
            w_veg_ptr[idx] = clampRange(growth, 0.0f, 1.0f);
        }
    }
}

std::string ResourcesSubsystem::name() const { return "resources"; }
std::vector<std::string> ResourcesSubsystem::declaredReadSet() const { return {"resources.fertility", "resources.vegetation", "resources.climate", "resources.current", "resources.water"}; }
std::vector<std::string> ResourcesSubsystem::declaredWriteSet() const { return {"resources.current"}; }

void ResourcesSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    (void)writeSession;
    (void)profile;
}

void ResourcesSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;
    double totalResources = 0.0;
    
    const auto h_fert = resolveReadHandle(stateStore, "resources.fertility", name());
    const auto h_veg = resolveReadHandle(stateStore, "resources.vegetation", name());
    const auto h_cli = resolveReadHandle(stateStore, "resources.climate", name());
    const auto h_res = resolveReadHandle(stateStore, "resources.current", name());
    const auto h_water = resolveReadHandle(stateStore, "resources.water", name());
    const auto w_res = resolveWriteHandle(stateStore, writeSession, "resources.current", name());
    
    const GridSpec& grid = stateStore.grid();
    const float* h_fert_ptr = stateStore.scalarFieldRawPtr(h_fert);
    const float* h_veg_ptr = stateStore.scalarFieldRawPtr(h_veg);
    const float* h_cli_ptr = stateStore.scalarFieldRawPtr(h_cli);
    const float* h_res_ptr = stateStore.scalarFieldRawPtr(h_res);
    const float* h_water_ptr = stateStore.scalarFieldRawPtr(h_water);
    float* w_res_ptr = stateStore.scalarFieldRawPtrMut(w_res);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);
    
    WS_OMP_PARALLEL_FOR_REDUCTION_SUM(totalResources)
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float fertility = h_fert_ptr[idx];
            const float vegetation = h_veg_ptr[idx];
            const float climate = h_cli_ptr[idx];
            
            const float up = (y > 0) ? h_res_ptr[idx - W] : h_res_ptr[idx];
            const float water = h_water_ptr[idx];
            float next = 0.25f + 0.35f * fertility + 0.25f * vegetation - 0.04f * std::fabs(climate);
            next += 0.06f * up;
            next += 0.10f * water;
            
            next = clampRange(next, 0.0f, 2.5f);
            w_res_ptr[idx] = next;
            totalResources += static_cast<double>(next);
        }
    }
    
    if (!std::isfinite(totalResources) || totalResources < 0.0) {
        throw std::runtime_error("ResourcesSubsystem conservation check failed: invalid resource mass");
    }
}

std::string EventSubsystem::name() const { return "events"; }
std::vector<std::string> EventSubsystem::declaredReadSet() const { return {"events.signal", "events.temperature", "events.humidity"}; }
std::vector<std::string> EventSubsystem::declaredWriteSet() const { return {"events.signal", "events.water_delta", "events.temperature_delta"}; }

void EventSubsystem::initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile&) {
    const auto signal = FieldResolver::resolveRequiredField(stateStore, "events.signal", name());
    const auto waterDelta = FieldResolver::resolveRequiredField(stateStore, "events.water_delta", name());
    const auto temperatureDelta = FieldResolver::resolveRequiredField(stateStore, "events.temperature_delta", name());
    writeSession.fillScalar(signal, 0.0f);
    writeSession.fillScalar(waterDelta, 0.0f);
    writeSession.fillScalar(temperatureDelta, 0.0f);
}

void EventSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    (void)profile;

    const float retention = 0.55f;
    const float exogenousScale = 0.06f;
    const float coolingScale = -0.30f;
    
    const auto h_event = resolveReadHandle(stateStore, "events.signal", name());
    const auto h_temp = resolveReadHandle(stateStore, "events.temperature", name());
    const auto h_hum = resolveReadHandle(stateStore, "events.humidity", name());

    const auto w_event = resolveWriteHandle(stateStore, writeSession, "events.signal", name());
    const auto w_water = resolveWriteHandle(stateStore, writeSession, "events.water_delta", name());
    const auto w_temp = resolveWriteHandle(stateStore, writeSession, "events.temperature_delta", name());

    const GridSpec& grid = stateStore.grid();
    const float* h_event_ptr = stateStore.scalarFieldRawPtr(h_event);
    const float* h_temp_ptr = stateStore.scalarFieldRawPtr(h_temp);
    const float* h_hum_ptr = stateStore.scalarFieldRawPtr(h_hum);
    float* w_event_ptr = stateStore.scalarFieldRawPtrMut(w_event);
    float* w_water_ptr = stateStore.scalarFieldRawPtrMut(w_water);
    float* w_temp_ptr = stateStore.scalarFieldRawPtrMut(w_temp);
    const std::size_t W = static_cast<std::size_t>(grid.width);
    const std::int64_t width = static_cast<std::int64_t>(grid.width);
    const std::int64_t height = static_cast<std::int64_t>(grid.height);

    WS_OMP_PARALLEL_FOR
    for (std::int64_t y = 0; y < height; ++y) {
        const std::size_t rowOffset = static_cast<std::size_t>(y) * W;
        for (std::int64_t x = 0; x < width; ++x) {
            const std::size_t idx = rowOffset + static_cast<std::size_t>(x);
            const float injectedSignal = h_event_ptr[idx];
            const float temperature = h_temp_ptr[idx];
            const float humidity = h_hum_ptr[idx];

            float trigger = 0.0f;
            if (temperature > 303.15f && humidity < 0.25f) {
                trigger = 0.12f;
            } else if (temperature > 301.15f && humidity < 0.35f) {
                trigger = 0.14f + 0.15f * clampRange((0.35f - humidity), 0.0f, 1.0f);
            } else if (temperature > 299.15f && humidity < 0.45f) {
                trigger = 0.20f + 0.20f * clampRange((0.45f - humidity), 0.0f, 1.0f);
            }

            const float retained = clampRange(injectedSignal * retention + trigger, 0.0f, 1.0f);
            w_event_ptr[idx] = retained;
            w_water_ptr[idx] = retained * exogenousScale;
            w_temp_ptr[idx] = retained * coolingScale;
        }
    }
}

std::vector<std::shared_ptr<ISubsystem>> makePhase4Subsystems() {
    return {
        std::make_shared<CellularAutomatonSubsystem>(),
        std::make_shared<ForestFireSubsystem>(),
        std::make_shared<GenerationSubsystem>(),
        std::make_shared<HydrologySubsystem>(),
        std::make_shared<TemperatureSubsystem>(),
        std::make_shared<HumiditySubsystem>(),
        std::make_shared<WindSubsystem>(),
        std::make_shared<ClimateSubsystem>(),
        std::make_shared<SoilSubsystem>(),
        std::make_shared<VegetationSubsystem>(),
        std::make_shared<ResourcesSubsystem>(),
        std::make_shared<EventSubsystem>()
    };
}

} // namespace ws
