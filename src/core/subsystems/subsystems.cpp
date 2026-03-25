#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace ws {

namespace {

ModelTier tierFor(const ModelProfile& profile, const std::string& subsystemName) {
    const auto it = profile.subsystemTiers.find(subsystemName);
    if (it == profile.subsystemTiers.end()) {
        throw std::runtime_error("Missing model tier for subsystem: " + subsystemName);
    }
    return it->second;
}

float clampRange(const float value, const float low, const float high) {
    return std::clamp(value, low, high);
}

float sampleCell(const StateStore& stateStore, const StateStore::FieldHandle handle, const Cell cell) {
    const auto sample = stateStore.trySampleScalarFast(
        handle,
        CellSigned{static_cast<std::int64_t>(cell.x), static_cast<std::int64_t>(cell.y)});
    return sample.value_or(0.0f);
}

float sampleOffset(const StateStore& stateStore, const StateStore::FieldHandle handle, const Cell cell, const std::int64_t dx, const std::int64_t dy) {
    const auto sample = stateStore.trySampleScalarFast(
        handle,
        CellSigned{static_cast<std::int64_t>(cell.x) + dx, static_cast<std::int64_t>(cell.y) + dy});
    return sample.value_or(0.0f);
}

void forEachCell(const StateStore& stateStore, const std::function<void(Cell)>& fn) {
    const GridSpec& grid = stateStore.grid();
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            fn(Cell{x, y});
        }
    }
}

} // namespace

std::string GenerationSubsystem::name() const { return "generation"; }
std::vector<std::string> GenerationSubsystem::declaredReadSet() const { return {"seed_probe", "terrain_elevation_h"}; }
std::vector<std::string> GenerationSubsystem::declaredWriteSet() const { return {"terrain_elevation_h"}; }

void GenerationSubsystem::initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    const auto h_seed = stateStore.getFieldHandle("seed_probe");
    const auto w_terrain = writeSession.getFieldHandle("terrain_elevation_h");

    forEachCell(stateStore, [&](const Cell cell) {
        const float seedProbe = sampleCell(stateStore, h_seed, cell);
        const float xNorm = static_cast<float>(cell.x) / static_cast<float>(std::max(1u, stateStore.grid().width - 1u));
        const float yNorm = static_cast<float>(cell.y) / static_cast<float>(std::max(1u, stateStore.grid().height - 1u));
        const float lowFreq = 0.5f * std::sin((xNorm + yNorm) * 3.14159265f);
        const float highFreq = 0.25f * std::cos((xNorm - yNorm) * 6.2831853f);

        float terrain = 0.0f;
        if (tier == ModelTier::A) {
            terrain = clampRange(0.6f + 0.2f * lowFreq + 0.05f * seedProbe, 0.0f, 1.0f);
        } else if (tier == ModelTier::B) {
            terrain = clampRange(0.55f + 0.2f * lowFreq + 0.15f * highFreq + 0.08f * seedProbe, 0.0f, 1.0f);
        } else {
            const float ridge = 0.12f * std::sin((2.0f * xNorm + yNorm) * 6.2831853f);
            terrain = clampRange(0.50f + 0.25f * lowFreq + 0.20f * highFreq + ridge + 0.10f * seedProbe, 0.0f, 1.0f);
        }

        writeSession.setScalarFast(w_terrain, cell, terrain);
    });
}

void GenerationSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) return;

    const auto h_terrain = stateStore.getFieldHandle("terrain_elevation_h");
    const auto w_terrain = writeSession.getFieldHandle("terrain_elevation_h");

    forEachCell(stateStore, [&](const Cell cell) {
        const float center = sampleCell(stateStore, h_terrain, cell);
        const float neighborhood = 0.25f * (
            sampleOffset(stateStore, h_terrain, cell, -1, 0) +
            sampleOffset(stateStore, h_terrain, cell, 1, 0) +
            sampleOffset(stateStore, h_terrain, cell, 0, -1) +
            sampleOffset(stateStore, h_terrain, cell, 0, 1));

        const float rate = (tier == ModelTier::B) ? 0.01f : 0.02f;
        const float eroded = clampRange(center + rate * (neighborhood - center), 0.0f, 1.0f);
        writeSession.setScalarFast(w_terrain, cell, eroded);
    });
}

std::string HydrologySubsystem::name() const { return "hydrology"; }
std::vector<std::string> HydrologySubsystem::declaredReadSet() const { return {"terrain_elevation_h", "humidity_q", "climate_index_c", "event_water_delta", "surface_water_w", "wind_u"}; }
std::vector<std::string> HydrologySubsystem::declaredWriteSet() const { return {"surface_water_w"}; }

void HydrologySubsystem::initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) {
        writeSession.fillScalar("surface_water_w", 0.18f);
    } else if (tier == ModelTier::B) {
        writeSession.fillScalar("surface_water_w", 0.24f);
    } else {
        writeSession.fillScalar("surface_water_w", 0.30f);
    }
    (void)stateStore;
}

void HydrologySubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    double total = 0.0;
    
    const auto h_elevation = stateStore.getFieldHandle("terrain_elevation_h");
    const auto h_humidity = stateStore.getFieldHandle("humidity_q");
    const auto h_climate = stateStore.getFieldHandle("climate_index_c");
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto h_eventWater = stateStore.getFieldHandle("event_water_delta");
    const auto h_wind = stateStore.getFieldHandle("wind_u");
    const auto w_water = writeSession.getFieldHandle("surface_water_w");

    forEachCell(stateStore, [&](const Cell cell) {
        const float elevation = sampleCell(stateStore, h_elevation, cell);
        const float humidity = sampleCell(stateStore, h_humidity, cell);
        const float climate = sampleCell(stateStore, h_climate, cell);
        const float priorWater = sampleCell(stateStore, h_water, cell);
        const float eventPulse = sampleCell(stateStore, h_eventWater, cell);

        float next = priorWater + 0.015f * humidity - 0.010f * elevation + 0.003f * climate + eventPulse;

        if (tier == ModelTier::B || tier == ModelTier::C) {
            const float neighborAvg = 0.25f * (
                sampleOffset(stateStore, h_water, cell, -1, 0) +
                sampleOffset(stateStore, h_water, cell, 1, 0) +
                sampleOffset(stateStore, h_water, cell, 0, -1) +
                sampleOffset(stateStore, h_water, cell, 0, 1));
            const float exchange = (tier == ModelTier::B) ? 0.08f : 0.16f;
            next += exchange * (neighborAvg - priorWater);
        }

        if (tier == ModelTier::C) {
            const float wind = sampleCell(stateStore, h_wind, cell);
            next += 0.015f * clampRange(wind, -8.0f, 8.0f);
        }

        next = clampRange(next, 0.0f, 2.0f);
        writeSession.setScalarFast(w_water, cell, next);
        total += static_cast<double>(next);
    });

    if (!std::isfinite(total) || total < 0.0) {
        throw std::runtime_error("HydrologySubsystem conservation check failed: invalid water mass");
    }
}

std::string TemperatureSubsystem::name() const { return "temperature"; }
std::vector<std::string> TemperatureSubsystem::declaredReadSet() const { return {"climate_index_c", "wind_u", "event_temperature_delta", "temperature_T", "humidity_q"}; }
std::vector<std::string> TemperatureSubsystem::declaredWriteSet() const { return {"temperature_T"}; }

void TemperatureSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) {
        writeSession.fillScalar("temperature_T", 285.15f);
    } else if (tier == ModelTier::B) {
        writeSession.fillScalar("temperature_T", 287.15f);
    } else {
        writeSession.fillScalar("temperature_T", 289.15f);
    }
}

void TemperatureSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t stepIndex) {
    const ModelTier tier = tierFor(profile, name());
    const float diurnal = static_cast<float>((stepIndex % 24u)) / 24.0f;
    
    // Check out Temperature Generation issue: "When displaying the temperature, it's the same everywhere."
    // Right now Temperature initializes to a flat field, and then adds diurnal shifts and climate index which might be initialized to flat as well. Wait, climate_index_c is flat to start with.
    // That's why temperature is totally flat to start! We should inject some variance based on terrain.
    // User requested "is the same everywhere" - actually, it just updates flatly as well if terrain isn't affecting it fast enough.
    // Terrain has elevation, we should add an elevation effect!
    const auto h_terrain = stateStore.getFieldHandle("terrain_elevation_h");

    const auto h_prior = stateStore.getFieldHandle("temperature_T");
    const auto h_climate = stateStore.getFieldHandle("climate_index_c");
    const auto h_wind = stateStore.getFieldHandle("wind_u");
    const auto h_eventDelta = stateStore.getFieldHandle("event_temperature_delta");
    const auto h_humidity = stateStore.getFieldHandle("humidity_q");
    const auto w_temp = writeSession.getFieldHandle("temperature_T");

    forEachCell(stateStore, [&](const Cell cell) {
        const float prior = sampleCell(stateStore, h_prior, cell);
        const float climate = sampleCell(stateStore, h_climate, cell);
        const float wind = sampleCell(stateStore, h_wind, cell);
        const float eventDelta = sampleCell(stateStore, h_eventDelta, cell);
        
        // Add a small terrain lapse rate so temperature immediately shows a gradient matching terrain
        const float elevation = sampleCell(stateStore, h_terrain, cell);
        const float terrainLapse = -5.0f * elevation;

        float next = prior + 0.05f * climate - 0.03f * std::fabs(wind) + eventDelta + 0.2f * (diurnal - 0.5f) + 0.01f * terrainLapse;
        if (tier == ModelTier::B || tier == ModelTier::C) {
            const float neighborAvg = 0.25f * (
                sampleOffset(stateStore, h_prior, cell, -1, 0) +
                sampleOffset(stateStore, h_prior, cell, 1, 0) +
                sampleOffset(stateStore, h_prior, cell, 0, -1) +
                sampleOffset(stateStore, h_prior, cell, 0, 1));
            const float blend = (tier == ModelTier::B) ? 0.06f : 0.10f;
            next += blend * (neighborAvg - prior);
        }

        if (tier == ModelTier::C) {
            const float humidity = sampleCell(stateStore, h_humidity, cell);
            next += 0.08f * (humidity - 0.5f);
        }

        next = clampRange(next, 220.0f, 340.0f);
        writeSession.setScalarFast(w_temp, cell, next);
    });
}

std::string HumiditySubsystem::name() const { return "humidity"; }
std::vector<std::string> HumiditySubsystem::declaredReadSet() const { return {"surface_water_w", "temperature_T", "vegetation_v", "humidity_q", "climate_index_c"}; }
std::vector<std::string> HumiditySubsystem::declaredWriteSet() const { return {"humidity_q"}; }

void HumiditySubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) {
        writeSession.fillScalar("humidity_q", 0.45f);
    } else if (tier == ModelTier::B) {
        writeSession.fillScalar("humidity_q", 0.55f);
    } else {
        writeSession.fillScalar("humidity_q", 0.62f);
    }
}

void HumiditySubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_veg = stateStore.getFieldHandle("vegetation_v");
    const auto h_hum = stateStore.getFieldHandle("humidity_q");
    const auto h_cli = stateStore.getFieldHandle("climate_index_c");
    const auto w_hum = writeSession.getFieldHandle("humidity_q");

    forEachCell(stateStore, [&](const Cell cell) {
        const float water = sampleCell(stateStore, h_water, cell);
        const float temp = sampleCell(stateStore, h_temp, cell);
        const float vegetation = sampleCell(stateStore, h_veg, cell);

        const float tempStress = clampRange((temp - 285.15f) / 40.0f, -1.0f, 1.0f);
        float next = 0.40f + 0.22f * water - 0.12f * tempStress + 0.08f * vegetation;
        if (tier == ModelTier::B || tier == ModelTier::C) {
            next += 0.03f * sampleOffset(stateStore, h_hum, cell, -1, 0);
        }

        if (tier == ModelTier::C) {
            const float climate = sampleCell(stateStore, h_cli, cell);
            next += 0.06f * climate;
        }

        next = clampRange(next, 0.0f, 1.0f);
        writeSession.setScalarFast(w_hum, cell, next);
    });
}

std::string WindSubsystem::name() const { return "wind"; }
std::vector<std::string> WindSubsystem::declaredReadSet() const { return {"temperature_T", "terrain_elevation_h", "humidity_q"}; }
std::vector<std::string> WindSubsystem::declaredWriteSet() const { return {"wind_u"}; }

void WindSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    writeSession.fillScalar("wind_u", 0.0f);
}

void WindSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_terrain = stateStore.getFieldHandle("terrain_elevation_h");
    const auto h_hum = stateStore.getFieldHandle("humidity_q");
    const auto w_wind = writeSession.getFieldHandle("wind_u");

    forEachCell(stateStore, [&](const Cell cell) {
        const float eastTemp = sampleOffset(stateStore, h_temp, cell, 1, 0);
        const float westTemp = sampleOffset(stateStore, h_temp, cell, -1, 0);
        const float eastTerrain = sampleOffset(stateStore, h_terrain, cell, 1, 0);
        const float westTerrain = sampleOffset(stateStore, h_terrain, cell, -1, 0);

        float next = 0.02f * (eastTemp - westTemp) - 0.08f * (eastTerrain - westTerrain);
        if (tier == ModelTier::B || tier == ModelTier::C) {
            const float northTemp = sampleOffset(stateStore, h_temp, cell, 0, -1);
            const float southTemp = sampleOffset(stateStore, h_temp, cell, 0, 1);
            next += 0.01f * (southTemp - northTemp);
        }

        if (tier == ModelTier::C) {
            const float southHumidity = sampleOffset(stateStore, h_hum, cell, 0, 1);
            const float northHumidity = sampleOffset(stateStore, h_hum, cell, 0, -1);
            next += 0.08f * (southHumidity - northHumidity);
        }

        writeSession.setScalarFast(w_wind, cell, clampRange(next, -8.0f, 8.0f));
    });
}

std::string ClimateSubsystem::name() const { return "climate"; }
std::vector<std::string> ClimateSubsystem::declaredReadSet() const { return {"temperature_T", "humidity_q", "wind_u", "climate_index_c", "surface_water_w"}; }
std::vector<std::string> ClimateSubsystem::declaredWriteSet() const { return {"climate_index_c"}; }

void ClimateSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    writeSession.fillScalar("climate_index_c", 0.0f);
}

void ClimateSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_hum = stateStore.getFieldHandle("humidity_q");
    const auto h_wind = stateStore.getFieldHandle("wind_u");
    const auto h_cli = stateStore.getFieldHandle("climate_index_c");
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto w_cli = writeSession.getFieldHandle("climate_index_c");

    forEachCell(stateStore, [&](const Cell cell) {
        const float temp = sampleCell(stateStore, h_temp, cell);
        const float humidity = sampleCell(stateStore, h_hum, cell);
        const float wind = sampleCell(stateStore, h_wind, cell);

        const float thermalTerm = clampRange((temp - 285.15f) / 20.0f, -3.0f, 3.0f);
        float next = 0.50f * thermalTerm + 0.80f * (humidity - 0.5f) - 0.12f * std::fabs(wind);

        if (tier == ModelTier::B || tier == ModelTier::C) {
            const float neighborhood = 0.25f * (
                sampleOffset(stateStore, h_cli, cell, -1, 0) +
                sampleOffset(stateStore, h_cli, cell, 1, 0) +
                sampleOffset(stateStore, h_cli, cell, 0, -1) +
                sampleOffset(stateStore, h_cli, cell, 0, 1));
            const float localWeight = (tier == ModelTier::B) ? 0.85f : 0.70f;
            const float neighborWeight = 1.0f - localWeight;
            next = localWeight * next + neighborWeight * neighborhood;
        }

        if (tier == ModelTier::C) {
            const float water = sampleCell(stateStore, h_water, cell);
            next += 0.12f * (water - 0.5f);
        }

        writeSession.setScalarFast(w_cli, cell, clampRange(next, -4.0f, 4.0f));
    });
}

std::string SoilSubsystem::name() const { return "soil"; }
std::vector<std::string> SoilSubsystem::declaredReadSet() const { return {"surface_water_w", "temperature_T", "fertility_phi", "climate_index_c"}; }
std::vector<std::string> SoilSubsystem::declaredWriteSet() const { return {"fertility_phi"}; }

void SoilSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) {
        writeSession.fillScalar("fertility_phi", 0.5f);
    } else if (tier == ModelTier::B) {
        writeSession.fillScalar("fertility_phi", 0.6f);
    } else {
        writeSession.fillScalar("fertility_phi", 0.68f);
    }
}

void SoilSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_fert = stateStore.getFieldHandle("fertility_phi");
    const auto h_cli = stateStore.getFieldHandle("climate_index_c");
    const auto w_fert = writeSession.getFieldHandle("fertility_phi");

    forEachCell(stateStore, [&](const Cell cell) {
        const float water = sampleCell(stateStore, h_water, cell);
        const float temp = sampleCell(stateStore, h_temp, cell);

        const float thermalSuitability = 1.0f - std::fabs((temp - 290.15f) / 35.0f);
        float next = 0.35f + 0.30f * water + 0.30f * clampRange(thermalSuitability, 0.0f, 1.0f);

        if (tier == ModelTier::B || tier == ModelTier::C) {
            next += 0.05f * sampleOffset(stateStore, h_fert, cell, 0, 1);
        }

        if (tier == ModelTier::C) {
            const float climate = sampleCell(stateStore, h_cli, cell);
            next -= 0.04f * std::fabs(climate);
        }

        writeSession.setScalarFast(w_fert, cell, clampRange(next, 0.0f, 1.0f));
    });
}

std::string VegetationSubsystem::name() const { return "vegetation"; }
std::vector<std::string> VegetationSubsystem::declaredReadSet() const { return {"fertility_phi", "humidity_q", "temperature_T", "resource_stock_r", "vegetation_v", "surface_water_w"}; }
std::vector<std::string> VegetationSubsystem::declaredWriteSet() const { return {"vegetation_v"}; }

void VegetationSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    writeSession.fillScalar("vegetation_v", 0.3f);
}

void VegetationSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    
    const auto h_fert = stateStore.getFieldHandle("fertility_phi");
    const auto h_hum = stateStore.getFieldHandle("humidity_q");
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_res = stateStore.getFieldHandle("resource_stock_r");
    const auto h_veg = stateStore.getFieldHandle("vegetation_v");
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto w_veg = writeSession.getFieldHandle("vegetation_v");


    forEachCell(stateStore, [&](const Cell cell) {
        const float fertility = sampleCell(stateStore, h_fert, cell);
        const float humidity = sampleCell(stateStore, h_hum, cell);
        const float temperature = sampleCell(stateStore, h_temp, cell);
        const float resources = sampleCell(stateStore, h_res, cell);

        const float thermalSuitability = 1.0f - std::fabs((temperature - 289.15f) / 40.0f);
        float growth = 0.20f * fertility + 0.20f * humidity + 0.10f * resources + 0.25f * clampRange(thermalSuitability, 0.0f, 1.0f);
        if (tier == ModelTier::B || tier == ModelTier::C) {
            growth += 0.08f * sampleOffset(stateStore, h_veg, cell, 1, 0);
        }

        if (tier == ModelTier::C) {
            const float water = sampleCell(stateStore, h_water, cell);
            growth += 0.12f * water;
        }

        writeSession.setScalarFast(w_veg, cell, clampRange(growth, 0.0f, 1.0f));
    });
}

std::string ResourcesSubsystem::name() const { return "resources"; }
std::vector<std::string> ResourcesSubsystem::declaredReadSet() const { return {"fertility_phi", "vegetation_v", "climate_index_c", "resource_stock_r", "surface_water_w"}; }
std::vector<std::string> ResourcesSubsystem::declaredWriteSet() const { return {"resource_stock_r"}; }

void ResourcesSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile& profile) {
    const ModelTier tier = tierFor(profile, name());
    if (tier == ModelTier::A) {
        writeSession.fillScalar("resource_stock_r", 0.4f);
    } else if (tier == ModelTier::B) {
        writeSession.fillScalar("resource_stock_r", 0.5f);
    } else {
        writeSession.fillScalar("resource_stock_r", 0.62f);
    }
}

void ResourcesSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());
    double totalResources = 0.0;
    
    const auto h_fert = stateStore.getFieldHandle("fertility_phi");
    const auto h_veg = stateStore.getFieldHandle("vegetation_v");
    const auto h_cli = stateStore.getFieldHandle("climate_index_c");
    const auto h_res = stateStore.getFieldHandle("resource_stock_r");
    const auto h_water = stateStore.getFieldHandle("surface_water_w");
    const auto w_res = writeSession.getFieldHandle("resource_stock_r");

    forEachCell(stateStore, [&](const Cell cell) {
        const float fertility = sampleCell(stateStore, h_fert, cell);
        const float vegetation = sampleCell(stateStore, h_veg, cell);
        const float climate = sampleCell(stateStore, h_cli, cell);

        float next = 0.25f + 0.35f * fertility + 0.25f * vegetation - 0.04f * std::fabs(climate);
        if (tier == ModelTier::B || tier == ModelTier::C) {
            next += 0.06f * sampleOffset(stateStore, h_res, cell, 0, -1);
        }

        if (tier == ModelTier::C) {
            const float water = sampleCell(stateStore, h_water, cell);
            next += 0.10f * water;
        }

        next = clampRange(next, 0.0f, 2.5f);
        writeSession.setScalarFast(w_res, cell, next);
        totalResources += static_cast<double>(next);
    });

    if (!std::isfinite(totalResources) || totalResources < 0.0) {
        throw std::runtime_error("ResourcesSubsystem conservation check failed: invalid resource mass");
    }
}

std::string EventSubsystem::name() const { return "events"; }
std::vector<std::string> EventSubsystem::declaredReadSet() const { return {"event_signal_e", "temperature_T", "humidity_q"}; }
std::vector<std::string> EventSubsystem::declaredWriteSet() const { return {"event_signal_e", "event_water_delta", "event_temperature_delta"}; }

void EventSubsystem::initialize(const StateStore&, StateStore::WriteSession& writeSession, const ModelProfile&) {
    writeSession.fillScalar("event_signal_e", 0.0f);
    writeSession.fillScalar("event_water_delta", 0.0f);
    writeSession.fillScalar("event_temperature_delta", 0.0f);
}

void EventSubsystem::step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, const std::uint64_t) {
    const ModelTier tier = tierFor(profile, name());

    const float retention = (tier == ModelTier::A) ? 0.35f : ((tier == ModelTier::B) ? 0.55f : 0.70f);
    const float exogenousScale = (tier == ModelTier::A) ? 0.03f : ((tier == ModelTier::B) ? 0.06f : 0.09f);
    const float coolingScale = (tier == ModelTier::A) ? -0.15f : ((tier == ModelTier::B) ? -0.30f : -0.45f);
    
    const auto h_event = stateStore.getFieldHandle("event_signal_e");
    const auto h_temp = stateStore.getFieldHandle("temperature_T");
    const auto h_hum = stateStore.getFieldHandle("humidity_q");
    
    const auto w_event = writeSession.getFieldHandle("event_signal_e");
    const auto w_water = writeSession.getFieldHandle("event_water_delta");
    const auto w_temp = writeSession.getFieldHandle("event_temperature_delta");

    forEachCell(stateStore, [&](const Cell cell) {
        const float injectedSignal = sampleCell(stateStore, h_event, cell);
        const float temperature = sampleCell(stateStore, h_temp, cell);
        const float humidity = sampleCell(stateStore, h_hum, cell);

        float trigger = 0.0f;
        if (tier == ModelTier::A) {
            if (temperature > 303.15f && humidity < 0.25f) {
                trigger = 0.12f;
            }
        } else if (tier == ModelTier::B) {
            if (temperature > 301.15f && humidity < 0.35f) {
                trigger = 0.14f + 0.15f * clampRange((0.35f - humidity), 0.0f, 1.0f);
            }
        } else {
            if (temperature > 299.15f && humidity < 0.45f) {
                trigger = 0.20f + 0.20f * clampRange((0.45f - humidity), 0.0f, 1.0f);
            }
        }

        const float retained = clampRange(injectedSignal * retention + trigger, 0.0f, 1.0f);
        writeSession.setScalarFast(w_event, cell, retained);
        writeSession.setScalarFast(w_water, cell, retained * exogenousScale);
        writeSession.setScalarFast(w_temp, cell, retained * coolingScale);
    });
}

std::vector<std::shared_ptr<ISubsystem>> makePhase4Subsystems() {
    return {
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
