#include "ws/app/shell_support.hpp"
#include "ws/core/determinism.hpp"
#include "ws/gui/runtime_service.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

std::vector<std::filesystem::path> discoverModels(const std::filesystem::path& modelsRoot) {
    std::vector<std::filesystem::path> models;
    for (const auto& entry : std::filesystem::directory_iterator(modelsRoot)) {
        if (!entry.is_directory() || entry.path().extension() != ".simmodel") {
            continue;
        }
        models.push_back(entry.path());
    }
    std::sort(models.begin(), models.end());
    return models;
}

std::string sanitizeToken(std::string value) {
    for (char& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }

    while (!value.empty() && value.front() == '_') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }

    return value.empty() ? "model" : value;
}

void require(bool ok, const std::string& context, const std::string& message) {
    if (!ok) {
        std::cerr << "[ui-state-transitions] unexpected failure context=" << context << " message=" << message << "\n";
        assert(false);
    }
}

bool hasWorld(const std::vector<ws::gui::StoredWorldInfo>& worlds, const std::string& worldName) {
    return std::any_of(worlds.begin(), worlds.end(), [&](const ws::gui::StoredWorldInfo& world) {
        return world.worldName == worldName;
    });
}

const ws::gui::StoredWorldInfo* findWorld(const std::vector<ws::gui::StoredWorldInfo>& worlds, const std::string& worldName) {
    const auto it = std::find_if(worlds.begin(), worlds.end(), [&](const ws::gui::StoredWorldInfo& world) {
        return world.worldName == worldName;
    });
    return it == worlds.end() ? nullptr : &(*it);
}

void runModelStateTransitions(const std::filesystem::path& modelPath, std::size_t index) {
    ws::gui::RuntimeService service;

    ws::gui::ModelScopeContext scope;
    scope.modelId = modelPath.stem().string();
    scope.modelName = modelPath.stem().string();
    scope.modelPath = modelPath.string();
    scope.modelIdentityHash = std::to_string(ws::DeterministicHash::hashString(modelPath.string()));
    service.setModelScope(scope);

    std::string message;
    const std::string token = sanitizeToken(modelPath.stem().string());
    const std::string worldA = "uistate_" + token + "_a_" + std::to_string(index);
    const std::string worldB = "uistate_" + token + "_b_" + std::to_string(index);
    const std::string worldARenamed = worldA + "_renamed";

    ws::app::LaunchConfig config = service.config();
    config.seed = ws::DeterministicHash::hashString(modelPath.string()) ^ 0x94D049BB133111EBull;
    config.grid = ws::GridSpec{10, 8};
    config.initialConditions.type = ws::InitialConditionType::Blank;

    require(service.createWorld(worldA, config, message), "create_world_a", message);
    require(service.configureCheckpointTimeline(2u, 64u, message), "timeline_config", message);
    require(service.step(6u, message), "step_for_hash_1", message);

    ws::RuntimeCheckpoint checkpointA1{};
    require(service.captureCheckpoint(checkpointA1, message, true), "capture_hash_1", message);

    require(service.restart(message), "restart", message);
    require(service.configureCheckpointTimeline(2u, 64u, message), "timeline_config_after_restart", message);
    require(service.step(6u, message), "step_for_hash_2", message);

    ws::RuntimeCheckpoint checkpointA2{};
    require(service.captureCheckpoint(checkpointA2, message, true), "capture_hash_2", message);
    assert(checkpointA1.stateSnapshot.stateHash == checkpointA2.stateSnapshot.stateHash);

    const std::string suggestedFromHint = service.suggestWorldNameFromHint("  Fancy New World ###  ");
    assert(!suggestedFromHint.empty());
    assert(service.normalizeWorldNameForUi("____Mixed---Name___") == "____Mixed---Name___");

    require(service.saveActiveWorld(message), "save_world_a", message);
    require(service.createWorld(worldB, config, message), "create_world_b", message);
    require(service.step(3u, message), "step_world_b", message);
    require(service.saveActiveWorld(message), "save_world_b", message);

    auto worlds = service.listStoredWorlds(message);
    assert(hasWorld(worlds, worldA));
    assert(hasWorld(worlds, worldB));
    const auto* worldAInfo = findWorld(worlds, worldA);
    const auto* worldBInfo = findWorld(worlds, worldB);
    assert(worldAInfo != nullptr && worldAInfo->hasProfile && worldAInfo->hasCheckpoint);
    assert(worldBInfo != nullptr && worldBInfo->hasProfile && worldBInfo->hasCheckpoint);
    assert(worldAInfo != nullptr && !worldAInfo->usesLegacyFallback());
    assert(worldBInfo != nullptr && !worldBInfo->usesLegacyFallback());

    require(service.renameWorld(worldA, worldARenamed, message), "rename_world_a", message);
    require(service.duplicateWorld(worldARenamed, worldA, message), "duplicate_world_a", message);

    worlds = service.listStoredWorlds(message);
    assert(hasWorld(worlds, worldA));
    assert(hasWorld(worlds, worldARenamed));
    assert(hasWorld(worlds, worldB));
    const auto* worldARenamedInfo = findWorld(worlds, worldARenamed);
    assert(worldARenamedInfo != nullptr && worldARenamedInfo->hasProfile && worldARenamedInfo->hasCheckpoint);
    assert(worldARenamedInfo != nullptr && !worldARenamedInfo->usesLegacyFallback());

    require(service.openWorld(worldARenamed, message), "open_world_renamed", message);
    require(service.pause(message), "pause", message);
    require(service.resume(message), "resume", message);

    ws::app::LaunchConfig config2 = service.config();
    config2.grid = ws::GridSpec{11, 9};
    config2.seed ^= 0xA24BAED4963EE407ull;
    service.setConfig(config2);
    require(service.applySettings(message), "apply_settings", message);
    require(service.step(2u, message), "step_after_apply_settings", message);

    require(service.stop(message), "stop", message);

    require(service.deleteWorld(worldA, message), "delete_world_a", message);
    require(service.deleteWorld(worldARenamed, message), "delete_world_renamed", message);
    require(service.deleteWorld(worldB, message), "delete_world_b", message);

    worlds = service.listStoredWorlds(message);
    assert(!hasWorld(worlds, worldA));
    assert(!hasWorld(worlds, worldARenamed));
    assert(!hasWorld(worlds, worldB));
}

} // namespace

int main() {
    const auto modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        std::cout << "Skipping ui_state_transitions_multi_model test: models root not found.\n";
        return 0;
    }

    const auto models = discoverModels(modelsRoot);
    if (models.empty()) {
        std::cout << "Skipping ui_state_transitions_multi_model test: no .simmodel directories found.\n";
        return 0;
    }

    std::size_t verified = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        runModelStateTransitions(models[i], i);
        ++verified;
    }

    std::cout << "ui_state_transitions_multi_model verified models=" << verified << "\n";
    return 0;
}
