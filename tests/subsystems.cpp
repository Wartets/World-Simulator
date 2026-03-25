#include "ws/core/profile.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

ws::ProfileResolverInput profileInputForTier(const ws::ModelTier tier) {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = tier;
    }
    input.compatibilityAssumptions = {
        "module_runtime",
        "profile_ab_only"
    };
    return input;
}

float fieldAverage(const ws::StateStoreSnapshot& snapshot, const std::string& variableName) {
    const auto it = std::find_if(
        snapshot.fields.begin(),
        snapshot.fields.end(),
        [&](const ws::StateStoreSnapshot::FieldPayload& field) {
            return field.spec.name == variableName;
        });

    if (it == snapshot.fields.end()) {
        throw std::runtime_error("Missing field in snapshot: " + variableName);
    }

    float total = 0.0f;
    for (const float value : it->values) {
        total += value;
    }

    if (it->values.empty()) {
        return 0.0f;
    }
    return total / static_cast<float>(it->values.size());
}

struct ScenarioResult {
    std::uint64_t identityHash = 0;
    std::uint64_t stateHash = 0;
    float avgWater = 0.0f;
    float avgTemperature = 0.0f;
    float avgResources = 0.0f;
};

ScenarioResult runScenario(const ws::ModelTier tier, const std::uint64_t seed) {
    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{8, 8};
    config.temporalPolicy = (tier == ws::ModelTier::B) ? ws::TemporalPolicy::PhasedB : ws::TemporalPolicy::UniformA;
    config.profileInput = profileInputForTier(tier);

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();

    ws::RuntimeEvent rainPulse;
    rainPulse.eventName = "exogenous_rain";
    rainPulse.scalarPatches.push_back(ws::ScalarWritePatch{"event_signal_e", ws::Cell{3, 4}, 0.9f});
    runtime.enqueueEvent(std::move(rainPulse));

    for (std::uint64_t step = 0; step < 32; ++step) {
        runtime.step();
    }

    const ws::RuntimeCheckpoint checkpoint = runtime.createCheckpoint("validation");

    ScenarioResult result;
    result.identityHash = runtime.snapshot().runSignature.identityHash();
    result.stateHash = runtime.snapshot().stateHash;
    result.avgWater = fieldAverage(checkpoint.stateSnapshot, "surface_water_w");
    result.avgTemperature = fieldAverage(checkpoint.stateSnapshot, "temperature_T");
    result.avgResources = fieldAverage(checkpoint.stateSnapshot, "resource_stock_r");

    runtime.stop();
    return result;
}

void verifyOwnershipContracts() {
    ws::Scheduler scheduler;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        scheduler.registerSubsystem(subsystem);
    }

    const auto ownership = scheduler.writeOwnershipByVariable();
    const std::vector<std::string> expectedOwnedVariables = {
        "terrain_elevation_h",
        "surface_water_w",
        "temperature_T",
        "humidity_q",
        "wind_u",
        "climate_index_c",
        "fertility_phi",
        "vegetation_v",
        "resource_stock_r",
        "event_signal_e",
        "event_water_delta",
        "event_temperature_delta"
    };

    for (const auto& variable : expectedOwnedVariables) {
        assert(ownership.contains(variable));
    }

    assert(ownership.at("terrain_elevation_h") == "generation");
    assert(ownership.at("surface_water_w") == "hydrology");
    assert(ownership.at("temperature_T") == "temperature");
    assert(ownership.at("event_signal_e") == "events");
}

void verifyInitializationAndStepping() {
    const ScenarioResult deterministicA1 = runScenario(ws::ModelTier::A, 777);
    const ScenarioResult deterministicA2 = runScenario(ws::ModelTier::A, 777);

    assert(deterministicA1.identityHash == deterministicA2.identityHash);
    assert(deterministicA1.stateHash == deterministicA2.stateHash);
    assert(deterministicA1.avgWater == deterministicA2.avgWater);
    assert(deterministicA1.avgTemperature == deterministicA2.avgTemperature);
    assert(deterministicA1.avgResources == deterministicA2.avgResources);

    const ScenarioResult tierB = runScenario(ws::ModelTier::B, 777);
    assert(deterministicA1.stateHash != tierB.stateHash);

    assert(deterministicA1.avgWater >= 0.0f);
    assert(deterministicA1.avgTemperature >= 220.0f);
    assert(deterministicA1.avgResources >= 0.0f);
}

void verifySubsystemReadWriteContractsExposeExplicitSets() {
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        const auto readSet = subsystem->declaredReadSet();
        const auto writeSet = subsystem->declaredWriteSet();

        assert(!writeSet.empty());
        if (subsystem->name() != "generation") {
            assert(!readSet.empty());
        }
    }
}

} // namespace

int main() {
    verifyOwnershipContracts();
    verifyInitializationAndStepping();
    verifySubsystemReadWriteContractsExposeExplicitSets();
    return 0;
}
