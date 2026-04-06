#include "ws/core/determinism.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/gui/runtime_service.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

std::filesystem::path resolveModelsRoot() {
    const std::filesystem::path direct = "models";
    if (std::filesystem::exists(direct) && std::filesystem::is_directory(direct)) {
        return direct;
    }

    const std::filesystem::path parent = std::filesystem::path("..") / "models";
    if (std::filesystem::exists(parent) && std::filesystem::is_directory(parent)) {
        return parent;
    }

    return {};
}

bool snapshotsFieldChanged(
    const ws::StateStoreSnapshot& a,
    const ws::StateStoreSnapshot& b,
    const std::string& fieldName) {
    const auto findField = [&](const ws::StateStoreSnapshot& snapshot) {
        return std::find_if(snapshot.fields.begin(), snapshot.fields.end(), [&](const auto& field) {
            return field.spec.name == fieldName;
        });
    };

    const auto itA = findField(a);
    const auto itB = findField(b);
    if (itA == a.fields.end() || itB == b.fields.end()) {
        return false;
    }

    const std::size_t n = std::min(itA->values.size(), itB->values.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(itA->values[i] - itB->values[i]) > 1e-6f) {
            return true;
        }
    }
    return false;
}

std::uint64_t hashField(const ws::StateStoreSnapshot& snapshot, const std::string& fieldName) {
    for (const auto& field : snapshot.fields) {
        if (field.spec.name != fieldName) {
            continue;
        }
        std::uint64_t h = ws::DeterministicHash::offsetBasis;
        for (const float value : field.values) {
            h = ws::DeterministicHash::combine(h, ws::DeterministicHash::hashPod(value));
        }
        return h;
    }
    return 0u;
}

void applyConwayBindingFromCatalog(
    const ws::initialization::ModelVariableCatalog& catalog,
    ws::app::LaunchConfig& config) {
    config.initialConditions.type = ws::InitialConditionType::Conway;

    ws::initialization::InitializationRequest request;
    request.type = ws::InitialConditionType::Conway;
    request.requireMetadataHints = false;
    const auto plan = ws::initialization::buildBindingPlan(catalog, request);
    assert(!plan.decisions.empty());
    assert(plan.decisions.front().resolved);
    config.initialConditions.conway.targetVariable = plan.decisions.front().variableId;
}

} // namespace

int main() {
    const auto modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        std::cout << "Skipping conway_activity_test: models root not found.\n";
        return 0;
    }

    const auto modelPath = modelsRoot / "game_of_life_model.simmodel";
    if (!std::filesystem::exists(modelPath)) {
        std::cout << "Skipping conway_activity_test: game_of_life model not found.\n";
        return 0;
    }

    ws::initialization::ModelVariableCatalog catalog;
    std::string catalogMessage;
    const bool loaded = ws::initialization::loadModelVariableCatalog(modelPath, catalog, catalogMessage);
    assert(loaded);

    ws::gui::RuntimeService service;
    ws::gui::ModelScopeContext scope;
    scope.modelId = "game_of_life_model";
    scope.modelName = "game_of_life_model";
    scope.modelPath = modelPath.string();
    scope.modelIdentityHash = std::to_string(ws::DeterministicHash::hashString(modelPath.string()));
    service.setModelScope(scope);

    ws::app::LaunchConfig config = service.config();
    config.seed = 0xC0FFEEu;
    config.grid = ws::GridSpec{128u, 128u};
    config.tier = ws::ModelTier::A;
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    applyConwayBindingFromCatalog(catalog, config);

    std::string message;
    const bool started = service.createWorld("conway_activity_world", config, message);
    assert(started);

    ws::RuntimeCheckpoint previous{};
    bool captured = service.captureCheckpoint(previous, message, false);
    assert(captured);

    std::set<std::uint64_t> uniqueLivingHashes;
    uniqueLivingHashes.insert(hashField(previous.stateSnapshot, "living"));

    std::uint32_t changedTransitions = 0u;
    for (std::uint32_t i = 0; i < 64u; ++i) {
        const bool stepped = service.step(1u, message);
        assert(stepped);

        ws::RuntimeCheckpoint current{};
        captured = service.captureCheckpoint(current, message, false);
        assert(captured);

        if (snapshotsFieldChanged(previous.stateSnapshot, current.stateSnapshot, "living")) {
            changedTransitions += 1u;
        }
        uniqueLivingHashes.insert(hashField(current.stateSnapshot, "living"));
        previous = std::move(current);
    }

    assert(changedTransitions > 0u);
    assert(uniqueLivingHashes.size() >= 3u);

    bool stopped = service.stop(message);
    assert(stopped);
    bool deleted = service.deleteWorld("conway_activity_world", message);
    assert(deleted);

    std::cout << "conway_activity_test transitions=" << changedTransitions
              << " unique_states=" << uniqueLivingHashes.size() << "\n";
    return 0;
}
