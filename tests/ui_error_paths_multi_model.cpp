#include "ws/app/shell_support.hpp"
#include "ws/core/determinism.hpp"
#include "ws/gui/runtime_service.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
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
        std::cerr << "[ui-error-paths] unexpected failure context=" << context << " message=" << message << "\n";
        assert(false);
    }
}

void requireFailure(bool ok, const std::string& context, const std::string& message, const std::string& expectedToken) {
    if (ok) {
        std::cerr << "[ui-error-paths] expected failure but succeeded context=" << context << "\n";
        assert(false);
    }
    if (!expectedToken.empty() && message.find(expectedToken) == std::string::npos) {
        std::cerr << "[ui-error-paths] failure token mismatch context=" << context
                  << " expected_token=" << expectedToken
                  << " message=" << message << "\n";
        assert(false);
    }
}

void runModelErrorPathScenarios(const std::filesystem::path& modelPath, std::size_t index) {
    ws::gui::RuntimeService service;

    ws::gui::ModelScopeContext scope;
    scope.modelId = modelPath.stem().string();
    scope.modelName = modelPath.stem().string();
    scope.modelPath = modelPath.string();
    scope.modelIdentityHash = std::to_string(ws::DeterministicHash::hashString(modelPath.string()));
    service.setModelScope(scope);

    std::string message;

    requireFailure(service.status(message), "status_before_start", message, "runtime is not ready");
    requireFailure(service.metrics(message), "metrics_before_start", message, "runtime is not ready");
    std::vector<std::string> preStartNames;
    requireFailure(service.fieldNames(preStartNames, message), "field_names_before_start", message, "runtime is not ready");
    requireFailure(service.step(1u, message), "step_before_start", message, "runtime is not ready");

    requireFailure(service.configureCheckpointTimeline(0u, 32u, message), "timeline_zero_interval", message, "interval_zero");
    requireFailure(service.setPlaybackSpeed(0.05f, message), "playback_too_low", message, "out_of_range");
    requireFailure(service.setPlaybackSpeed(10.0f, message), "playback_too_high", message, "out_of_range");

    ws::app::LaunchConfig config = service.config();
    config.seed = ws::DeterministicHash::hashString(modelPath.string()) ^ 0xD1B54A32D192ED03ull;
    config.grid = ws::GridSpec{9, 7};
    config.initialConditions.type = ws::InitialConditionType::Blank;

    const std::string worldName = "uierr_" + sanitizeToken(modelPath.stem().string()) + "_" + std::to_string(index);

    requireFailure(service.createWorld("", config, message), "create_world_empty", message, "world name is required");
    require(service.createWorld(worldName, config, message), "create_world", message);

    requireFailure(service.step(0u, message), "step_zero", message, "positive");
    requireFailure(service.stepBackward(0u, message), "step_backward_zero", message, "zero_count");
    requireFailure(service.summarizeField("", message), "summary_empty", message, "requires a variable name");
    requireFailure(service.summarizeField("not_a_real_field", message), "summary_unknown", message, "unknown variable");

    requireFailure(service.createCheckpoint("", message), "checkpoint_empty_label", message, "label is required");
    requireFailure(service.restoreCheckpoint("missing", message), "restore_unknown_checkpoint", message, "unknown checkpoint");

    requireFailure(service.saveProfile("", message), "save_profile_empty", message, "profile name is required");
    requireFailure(service.loadProfile("", message), "load_profile_empty", message, "profile name is required");
    requireFailure(service.openWorld("", message), "open_world_empty", message, "world name is required");

    require(service.pause(message), "pause", message);
    require(service.resume(message), "resume", message);

    require(service.stop(message), "stop", message);
    require(service.stop(message), "stop_idempotent", message);

    requireFailure(service.saveActiveWorld(message), "save_active_world_stopped", message, "requires an active running runtime");

    require(service.deleteWorld(worldName, message), "delete_world", message);
}

} // namespace

int main() {
    const auto modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        std::cout << "Skipping ui_error_paths_multi_model test: models root not found.\n";
        return 0;
    }

    const auto models = discoverModels(modelsRoot);
    if (models.empty()) {
        std::cout << "Skipping ui_error_paths_multi_model test: no .simmodel directories found.\n";
        return 0;
    }

    std::size_t verified = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        runModelErrorPathScenarios(models[i], i);
        ++verified;
    }

    std::cout << "ui_error_paths_multi_model verified models=" << verified << "\n";
    return 0;
}
