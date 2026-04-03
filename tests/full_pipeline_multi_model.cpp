#include "ws/app/shell_support.hpp"
#include "ws/core/determinism.hpp"
#include "ws/gui/display_manager.hpp"
#include "ws/gui/runtime_service.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
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

    if (value.empty()) {
        return "model";
    }
    return value;
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

void require(const bool ok, const std::string& context, const std::string& message) {
    if (!ok) {
        std::cerr << "[pipeline-failure] context=" << context << " message=" << message << "\n";
        assert(false);
    }
}

bool hasWorld(const std::vector<ws::gui::StoredWorldInfo>& worlds, const std::string& worldName) {
    return std::any_of(
        worlds.begin(),
        worlds.end(),
        [&](const ws::gui::StoredWorldInfo& world) {
            return world.worldName == worldName;
        });
}

void verifyDisplayPipeline(
    const ws::RuntimeCheckpoint& checkpoint,
    const std::unordered_map<std::string, std::vector<std::string>>& displayTags) {
    const std::size_t expectedCellCount =
        static_cast<std::size_t>(checkpoint.stateSnapshot.grid.width) *
        static_cast<std::size_t>(checkpoint.stateSnapshot.grid.height);

    ws::gui::DisplayManagerParams displayParams;
    for (int displayType = static_cast<int>(ws::gui::DisplayType::ScalarField);
         displayType <= static_cast<int>(ws::gui::DisplayType::WindField);
         ++displayType) {
        const auto buffer = ws::gui::buildDisplayBufferFromSnapshot(
            checkpoint.stateSnapshot,
            0,
            static_cast<ws::gui::DisplayType>(displayType),
            true,
            displayParams,
            displayTags);

        assert(!buffer.values.empty());
        assert(buffer.values.size() == expectedCellCount);
        assert(!buffer.label.empty());

        float finiteCount = 0.0f;
        for (const float value : buffer.values) {
            if (std::isfinite(value)) {
                finiteCount += 1.0f;
            }
        }
        assert(finiteCount > 0.0f);
    }

    displayParams.autoWaterLevel = false;
    displayParams.waterLevel = 0.62f;
    displayParams.lowlandThreshold = 0.42f;
    displayParams.highlandThreshold = 0.78f;

    const auto remappedBuffer = ws::gui::buildDisplayBufferFromSnapshot(
        checkpoint.stateSnapshot,
        0,
        ws::gui::DisplayType::SurfaceCategory,
        true,
        displayParams,
        displayTags);
    assert(remappedBuffer.values.size() == expectedCellCount);

    const auto& terrain = checkpoint.stateSnapshot.fields.front().values;
    const std::vector<float> water(terrain.size(), 0.0f);
    const auto previewBuffer = ws::gui::buildDisplayBufferFromTerrain(
        terrain,
        water,
        ws::gui::DisplayType::MoistureMap,
        displayParams,
        "pipeline_preview");
    assert(previewBuffer.values.size() == terrain.size());
}

void verifyParameterAndPatchPipeline(
    ws::gui::RuntimeService& service,
    const std::vector<std::string>& fieldNames) {
    std::string message;

    require(service.pause(message), "pause_before_parameter_patch", message);

    std::vector<ws::ParameterControl> controls;
    require(service.parameterControls(controls, message), "parameter_controls", message);
    if (!controls.empty()) {
        const auto& control = controls.front();
        const float target = 0.5f * (control.minValue + control.maxValue);
        require(service.setParameterValue(control.name, target, "pipeline_parameter_update", message), "set_parameter_midpoint", message);

        require(service.setParameterValue(control.name, control.minValue, "pipeline_parameter_min", message), "set_parameter_min", message);
        require(service.setParameterValue(control.name, control.maxValue, "pipeline_parameter_max", message), "set_parameter_max", message);
    }

    assert(!fieldNames.empty());
    const std::string patchTarget = fieldNames.front();
    require(service.applyManualPatch(patchTarget, ws::Cell{0u, 0u}, 0.25f, "pipeline_manual_patch", message), "manual_patch_cell", message);
    require(service.step(1u, message), "step_after_manual_patch", message);

    require(service.applyManualPatch(patchTarget, std::nullopt, 0.05f, "pipeline_manual_patch_global", message), "manual_patch_global", message);
    require(service.step(1u, message), "step_after_global_patch", message);

    require(service.undoLastManualPatch(message), "undo_manual_patch", message);
    require(service.step(1u, message), "step_after_undo", message);

    const std::array<ws::PerturbationType, 5> perturbationTypes = {
        ws::PerturbationType::Gaussian,
        ws::PerturbationType::Rectangle,
        ws::PerturbationType::Sine,
        ws::PerturbationType::WhiteNoise,
        ws::PerturbationType::Gradient
    };
    for (std::size_t i = 0; i < perturbationTypes.size(); ++i) {
        ws::PerturbationSpec perturbation;
        perturbation.type = perturbationTypes[i];
        perturbation.targetVariable = patchTarget;
        perturbation.amplitude = 0.05f + 0.01f * static_cast<float>(i);
        perturbation.startStep = 0u;
        perturbation.durationSteps = 1u;
        perturbation.origin = ws::Cell{0u, 0u};
        perturbation.width = 2u;
        perturbation.height = 2u;
        perturbation.sigma = 1.5f;
        perturbation.frequency = 0.3f;
        perturbation.noiseSeed = 1000u + static_cast<std::uint64_t>(i);
        perturbation.description = "pipeline_perturbation_" + std::to_string(i);

        require(service.enqueuePerturbation(perturbation, message), "enqueue_perturbation", message);
        require(service.step(1u, message), "step_after_perturbation", message);
    }

    std::vector<ws::ManualEventRecord> events;
    require(service.manualEventLog(events, message), "manual_event_log", message);
    assert(!events.empty());

    require(service.resume(message), "resume_after_parameter_patch", message);
}

void verifyTimelineAndSeeking(ws::gui::RuntimeService& service) {
    std::string message;
    require(service.configureCheckpointTimeline(1u, 64u, message), "configure_timeline", message);
    require(service.step(4u, message), "timeline_seed_steps", message);

    ws::RuntimeCheckpoint snapshot{};
    require(service.captureCheckpoint(snapshot, message, false), "capture_for_timeline", message);
    const std::uint64_t stepBefore = snapshot.stateSnapshot.header.stepIndex;

    require(service.runUntil(stepBefore + 6u, message), "run_until", message);
    require(service.captureCheckpoint(snapshot, message, false), "capture_after_run_until", message);
    assert(snapshot.stateSnapshot.header.stepIndex >= stepBefore + 6u);

    const std::uint64_t seekTarget = snapshot.stateSnapshot.header.stepIndex > 3u
        ? snapshot.stateSnapshot.header.stepIndex - 3u
        : 0u;
    require(service.seekStep(seekTarget, message), "seek_backward", message);
    require(service.captureCheckpoint(snapshot, message, false), "capture_after_seek", message);
    assert(snapshot.stateSnapshot.header.stepIndex == seekTarget);

    require(service.stepBackward(1u, message), "step_backward", message);
    require(service.captureCheckpoint(snapshot, message, false), "capture_after_step_backward", message);
    assert(snapshot.stateSnapshot.header.stepIndex <= seekTarget);

    require(service.setPlaybackSpeed(1.75f, message), "set_playback_speed", message);
    assert(std::fabs(service.playbackSpeed() - 1.75f) < 1e-6f);
}

void verifyProbePipeline(ws::gui::RuntimeService& service, const std::vector<std::string>& fieldNames) {
    std::string message;
    assert(!fieldNames.empty());
    const std::string& variable = fieldNames.front();

    ws::ProbeDefinition globalProbe;
    globalProbe.id = "pipeline_probe_global";
    globalProbe.kind = ws::ProbeKind::GlobalScalar;
    globalProbe.variableName = variable;
    require(service.addProbe(globalProbe, message), "add_probe_global", message);

    ws::ProbeDefinition cellProbe;
    cellProbe.id = "pipeline_probe_cell";
    cellProbe.kind = ws::ProbeKind::CellScalar;
    cellProbe.variableName = variable;
    cellProbe.cell = ws::Cell{0u, 0u};
    require(service.addProbe(cellProbe, message), "add_probe_cell", message);

    ws::ProbeDefinition regionProbe;
    regionProbe.id = "pipeline_probe_region";
    regionProbe.kind = ws::ProbeKind::RegionAverage;
    regionProbe.variableName = variable;
    regionProbe.region.min = ws::Cell{0u, 0u};
    regionProbe.region.max = ws::Cell{1u, 1u};
    require(service.addProbe(regionProbe, message), "add_probe_region", message);

    require(service.step(2u, message), "step_for_probes", message);

    std::vector<ws::ProbeDefinition> definitions;
    require(service.probeDefinitions(definitions, message), "probe_definitions", message);
    assert(definitions.size() >= 3u);

    ws::ProbeSeries globalSeries;
    require(service.probeSeries(globalProbe.id, globalSeries, message), "probe_series_global", message);
    assert(!globalSeries.samples.empty());

    ws::ProbeSeries cellSeries;
    require(service.probeSeries(cellProbe.id, cellSeries, message), "probe_series_cell", message);
    assert(!cellSeries.samples.empty());

    require(service.removeProbe(cellProbe.id, message), "remove_probe_cell", message);
    require(service.clearProbes(message), "clear_probes", message);
}

void verifyCheckpointAndDiagnostics(ws::gui::RuntimeService& service) {
    std::string message;
    require(service.createCheckpoint("pipeline_cp_a", message), "create_checkpoint_a", message);
    require(service.step(2u, message), "step_between_checkpoints", message);
    require(service.createCheckpoint("pipeline_cp_b", message), "create_checkpoint_b", message);

    require(service.restoreCheckpoint("pipeline_cp_a", message), "restore_checkpoint_a", message);

    ws::StepDiagnostics diagnostics{};
    require(service.lastStepDiagnostics(diagnostics, message), "last_step_diagnostics", message);
    assert(!diagnostics.orderingLog.empty());

    require(service.status(message), "status", message);
    assert(message.find("status=running") != std::string::npos);

    require(service.metrics(message), "metrics", message);
    assert(message.find("metrics") != std::string::npos);

    require(service.listFields(message), "list_fields", message);
    assert(message.find("fields count=") != std::string::npos);
}

void runModelPipeline(const std::filesystem::path& modelPath, const std::size_t index) {
    ws::gui::RuntimeService service;

    ws::gui::ModelScopeContext scope;
    scope.modelId = modelPath.stem().string();
    scope.modelName = modelPath.stem().string();
    scope.modelPath = modelPath.string();
    scope.modelIdentityHash = std::to_string(ws::DeterministicHash::hashString(modelPath.string()));
    service.setModelScope(scope);

    ws::app::LaunchConfig config = service.config();
    config.seed = ws::DeterministicHash::hashString(modelPath.string()) ^ 0x9e3779b97f4a7c15ull;
    config.grid = ws::GridSpec{12, 10};
    config.tier = ws::ModelTier::A;
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.initialConditions.type = ws::InitialConditionType::Blank;

    std::string message;
    const std::string baseToken = sanitizeToken(modelPath.stem().string());
    const std::string worldName = "pipeline_" + baseToken + "_" + std::to_string(index);
    const std::string worldCopyName = worldName + "_copy";
    const std::string worldRenamedName = worldName + "_renamed";
    const std::string profileName = "pipeline_profile_" + baseToken + "_" + std::to_string(index);

    require(service.createWorld(worldName, config, message), "create_world", message);
    assert(service.activeWorldName() == worldName);
    assert(service.isRunning());

    auto worlds = service.listStoredWorlds(message);
    assert(hasWorld(worlds, worldName));

    require(service.step(3u, message), "initial_steps", message);

    std::vector<std::string> fieldNames;
    require(service.fieldNames(fieldNames, message), "field_names", message);
    assert(!fieldNames.empty());

    verifyTimelineAndSeeking(service);
    verifyProbePipeline(service, fieldNames);
    verifyParameterAndPatchPipeline(service, fieldNames);
    verifyCheckpointAndDiagnostics(service);

    ws::RuntimeCheckpoint checkpoint{};
    require(service.captureCheckpoint(checkpoint, message, false), "capture_checkpoint", message);
    assert(!checkpoint.stateSnapshot.fields.empty());

    std::unordered_map<std::string, std::vector<std::string>> displayTags;
    require(service.fieldDisplayTags(displayTags, message), "field_display_tags", message);
    verifyDisplayPipeline(checkpoint, displayTags);

    require(service.summarizeField(fieldNames.front(), message), "summarize_field", message);
    assert(message.find("summary variable=") != std::string::npos);

    require(service.saveProfile(profileName, message), "save_profile", message);
    require(service.listProfiles(message), "list_profiles", message);
    assert(message.find(profileName) != std::string::npos);
    require(service.loadProfile(profileName, message), "load_profile", message);

    require(service.saveActiveWorld(message), "save_active_world", message);
    require(service.duplicateWorld(worldName, worldCopyName, message), "duplicate_world", message);
    require(service.renameWorld(worldCopyName, worldRenamedName, message), "rename_world", message);

    const auto exportPath = std::filesystem::path("checkpoints") / "world_exports" / (worldName + ".wsexport");
    require(service.exportWorld(worldName, exportPath, message), "export_world", message);

    std::string importedWorldName;
    require(service.importWorld(exportPath, importedWorldName, message), "import_world", message);
    assert(!importedWorldName.empty());

    worlds = service.listStoredWorlds(message);
    assert(hasWorld(worlds, worldName));
    assert(hasWorld(worlds, worldRenamedName));
    assert(hasWorld(worlds, importedWorldName));

    require(service.stop(message), "stop_before_reopen", message);
    require(service.openWorld(worldName, message), "open_world", message);
    assert(service.activeWorldName() == worldName);

    ws::RuntimeCheckpoint reopenedCheckpoint{};
    require(service.captureCheckpoint(reopenedCheckpoint, message, false), "capture_reopened_checkpoint", message);
    assert(!reopenedCheckpoint.stateSnapshot.fields.empty());

    require(service.applySettings(message), "apply_settings", message);
    assert(service.isRunning());

    require(service.stop(message), "final_stop", message);
    require(service.deleteWorld(worldName, message), "delete_world_original", message);
    require(service.deleteWorld(worldRenamedName, message), "delete_world_renamed", message);
    require(service.deleteWorld(importedWorldName, message), "delete_world_imported", message);

    worlds = service.listStoredWorlds(message);
    assert(!hasWorld(worlds, worldName));
    assert(!hasWorld(worlds, worldRenamedName));
    assert(!hasWorld(worlds, importedWorldName));

    std::error_code ec;
    std::filesystem::remove(std::filesystem::path("profiles") / (profileName + ".wsprofile"), ec);
    std::filesystem::remove(exportPath, ec);
}

} // namespace

int main() {
    const auto modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        std::cout << "Skipping full_pipeline_multi_model test: models root not found.\n";
        return 0;
    }

    const auto models = discoverModels(modelsRoot);
    if (models.empty()) {
        std::cout << "Skipping full_pipeline_multi_model test: no .simmodel directories found.\n";
        return 0;
    }

    std::size_t verified = 0;
    for (std::size_t i = 0; i < models.size(); ++i) {
        runModelPipeline(models[i], i);
        ++verified;
    }

    std::cout << "full_pipeline_multi_model verified models=" << verified << "\n";
    return 0;
}
