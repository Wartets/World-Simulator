#include "ws/app/profile_store.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

void verifyInitialConditionRoundTrip() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ws_profile_store_test";
    std::filesystem::create_directories(root);

    ws::app::ProfileStore store(root);
    ws::app::LaunchConfig config;
    config.seed = 123456789ULL;
    config.grid = ws::GridSpec{96, 64};
    config.tier = ws::ModelTier::C;
    config.temporalPolicy = ws::TemporalPolicy::MultiRateC;
    config.timeIntegratorId = "RK4";

    config.initialConditions.type = ws::InitialConditionType::GrayScott;
    config.initialConditions.terrain.terrainBaseFrequency = 3.4f;
    config.initialConditions.conway.targetVariable = "initialization.conway.target";
    config.initialConditions.conway.aliveProbability = 0.37f;
    config.initialConditions.conway.aliveValue = 2.0f;
    config.initialConditions.conway.deadValue = -1.0f;
    config.initialConditions.conway.smoothingPasses = 3;

    config.initialConditions.grayScott.targetVariableA = "initialization.gray_scott.target_a";
    config.initialConditions.grayScott.targetVariableB = "initialization.gray_scott.target_b";
    config.initialConditions.grayScott.backgroundA = 0.9f;
    config.initialConditions.grayScott.backgroundB = 0.1f;
    config.initialConditions.grayScott.spotValueA = 0.2f;
    config.initialConditions.grayScott.spotValueB = 0.8f;
    config.initialConditions.grayScott.spotCount = 7;
    config.initialConditions.grayScott.spotRadius = 11.5f;
    config.initialConditions.grayScott.spotJitter = 0.42f;

    config.initialConditions.waves.targetVariable = "initialization.waves.target";
    config.initialConditions.waves.baseline = 0.2f;
    config.initialConditions.waves.dropAmplitude = 1.3f;
    config.initialConditions.waves.dropRadius = 9.0f;
    config.initialConditions.waves.dropCount = 5;
    config.initialConditions.waves.dropJitter = 0.25f;
    config.initialConditions.waves.ringFrequency = 1.7f;

    const std::string profileName = "roundtrip";
    store.save(profileName, config);

    const ws::app::LaunchConfig loaded = store.load(profileName);

    assert(loaded.seed == config.seed);
    assert(loaded.grid.width == config.grid.width);
    assert(loaded.grid.height == config.grid.height);
    assert(loaded.tier == config.tier);
    assert(loaded.temporalPolicy == config.temporalPolicy);
    assert(loaded.timeIntegratorId == "rk4");
    assert(loaded.initialConditions.type == config.initialConditions.type);

    assert(loaded.initialConditions.conway.targetVariable == config.initialConditions.conway.targetVariable);
    assert(loaded.initialConditions.conway.aliveProbability == config.initialConditions.conway.aliveProbability);
    assert(loaded.initialConditions.conway.aliveValue == config.initialConditions.conway.aliveValue);
    assert(loaded.initialConditions.conway.deadValue == config.initialConditions.conway.deadValue);
    assert(loaded.initialConditions.conway.smoothingPasses == config.initialConditions.conway.smoothingPasses);

    assert(loaded.initialConditions.grayScott.targetVariableA == config.initialConditions.grayScott.targetVariableA);
    assert(loaded.initialConditions.grayScott.targetVariableB == config.initialConditions.grayScott.targetVariableB);
    assert(loaded.initialConditions.grayScott.backgroundA == config.initialConditions.grayScott.backgroundA);
    assert(loaded.initialConditions.grayScott.backgroundB == config.initialConditions.grayScott.backgroundB);
    assert(loaded.initialConditions.grayScott.spotValueA == config.initialConditions.grayScott.spotValueA);
    assert(loaded.initialConditions.grayScott.spotValueB == config.initialConditions.grayScott.spotValueB);
    assert(loaded.initialConditions.grayScott.spotCount == config.initialConditions.grayScott.spotCount);
    assert(loaded.initialConditions.grayScott.spotRadius == config.initialConditions.grayScott.spotRadius);
    assert(loaded.initialConditions.grayScott.spotJitter == config.initialConditions.grayScott.spotJitter);

    assert(loaded.initialConditions.waves.targetVariable == config.initialConditions.waves.targetVariable);
    assert(loaded.initialConditions.waves.baseline == config.initialConditions.waves.baseline);
    assert(loaded.initialConditions.waves.dropAmplitude == config.initialConditions.waves.dropAmplitude);
    assert(loaded.initialConditions.waves.dropRadius == config.initialConditions.waves.dropRadius);
    assert(loaded.initialConditions.waves.dropCount == config.initialConditions.waves.dropCount);
    assert(loaded.initialConditions.waves.dropJitter == config.initialConditions.waves.dropJitter);
    assert(loaded.initialConditions.waves.ringFrequency == config.initialConditions.waves.ringFrequency);

    std::error_code ec;
    std::filesystem::remove(store.pathFor(profileName), ec);
}

} // namespace

int main() {
    verifyInitialConditionRoundTrip();
    return 0;
}
