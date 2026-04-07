#include "ws/app/shell_support.hpp"
#include "ws/core/determinism.hpp"
#include "ws/core/initialization_binding.hpp"
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

std::optional<std::string> tryAliasTarget(
    const ws::ModelExecutionSpec& executionSpec,
    const std::string& semanticKey,
    const std::vector<std::string>& fieldNames) {
    const auto aliasIt = executionSpec.semanticFieldAliases.find(semanticKey);
    if (aliasIt == executionSpec.semanticFieldAliases.end() || aliasIt->second.empty()) {
        return std::nullopt;
    }

    if (std::find(fieldNames.begin(), fieldNames.end(), aliasIt->second) == fieldNames.end()) {
        return std::nullopt;
    }
    return aliasIt->second;
}

bool isDiscreteLikeField(const ws::initialization::ModelVariableCatalog& catalog, const std::string& variableName) {
    const auto it = std::find_if(catalog.variables.begin(), catalog.variables.end(), [&](const auto& variable) {
        return variable.id == variableName;
    });
    if (it == catalog.variables.end()) {
        return variableName == "living" || variableName == "fire_state";
    }

    if (it->type == "bool" || it->type == "u32" || it->type == "i32") {
        return true;
    }
    return variableName == "living" || variableName == "fire_state";
}

std::string chooseRepresentativeField(
    const std::vector<std::string>& fieldNames,
    const ws::initialization::ModelVariableCatalog& catalog,
    const ws::ModelExecutionSpec& executionSpec) {
    if (fieldNames.empty()) {
        return {};
    }

    const std::array<const char*, 8> preferredAliases = {
        "automaton.state",
        "fire.state",
        "temperature.current",
        "hydrology.water",
        "climate.current",
        "vegetation.current",
        "resources.current",
        "generation.elevation"
    };
    for (const char* semanticKey : preferredAliases) {
        const auto alias = tryAliasTarget(executionSpec, semanticKey, fieldNames);
        if (alias.has_value()) {
            return *alias;
        }
    }

    if (!catalog.preferredDisplayVariable.empty() &&
        std::find(fieldNames.begin(), fieldNames.end(), catalog.preferredDisplayVariable) != fieldNames.end()) {
        return catalog.preferredDisplayVariable;
    }

    for (const auto& variable : catalog.variables) {
        if (variable.support == "cell" && variable.role == "state" &&
            std::find(fieldNames.begin(), fieldNames.end(), variable.id) != fieldNames.end()) {
            return variable.id;
        }
    }

    return fieldNames.front();
}

float preferredCellPatchValue(
    const ws::initialization::ModelVariableCatalog& catalog,
    const std::string& variableName) {
    return isDiscreteLikeField(catalog, variableName) ? 1.0f : 0.25f;
}

float preferredGlobalPatchValue(
    const ws::initialization::ModelVariableCatalog& catalog,
    const std::string& variableName) {
    return isDiscreteLikeField(catalog, variableName) ? 0.0f : 0.05f;
}

float preferredEvolutionKickValue(
    const ws::initialization::ModelVariableCatalog& catalog,
    const std::string& variableName) {
    return isDiscreteLikeField(catalog, variableName) ? 1.0f : 0.125f;
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

bool snapshotsEvolved(
    const ws::StateStoreSnapshot& before,
    const ws::StateStoreSnapshot& after) {
    const std::size_t fieldCount = std::min(before.fields.size(), after.fields.size());
    for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
        const auto& beforeValues = before.fields[fieldIndex].values;
        const auto& afterValues = after.fields[fieldIndex].values;
        const std::size_t valueCount = std::min(beforeValues.size(), afterValues.size());
        for (std::size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex) {
            if (std::fabs(beforeValues[valueIndex] - afterValues[valueIndex]) > 1e-6f) {
                return true;
            }
        }
    }
    return false;
}

bool fieldEvolved(
    const ws::StateStoreSnapshot& before,
    const ws::StateStoreSnapshot& after,
    const std::string& variableName) {
    const auto beforeIt = std::find_if(before.fields.begin(), before.fields.end(), [&](const auto& field) {
        return field.spec.name == variableName;
    });
    const auto afterIt = std::find_if(after.fields.begin(), after.fields.end(), [&](const auto& field) {
        return field.spec.name == variableName;
    });
    if (beforeIt == before.fields.end() || afterIt == after.fields.end()) {
        return false;
    }

    const std::size_t valueCount = std::min(beforeIt->values.size(), afterIt->values.size());
    for (std::size_t i = 0; i < valueCount; ++i) {
        if (std::fabs(beforeIt->values[i] - afterIt->values[i]) > 1e-6f) {
            return true;
        }
    }
    return false;
}

void applyBindingPlanToConfig(
    const ws::initialization::InitializationBindingPlan& plan,
    ws::app::LaunchConfig& config) {
    for (const auto& decision : plan.decisions) {
        if (!decision.resolved || decision.variableId.empty()) {
            continue;
        }

        if (decision.bindingKey == "conway.target_variable") {
            config.initialConditions.conway.targetVariable = decision.variableId;
            continue;
        }
        if (decision.bindingKey == "gray_scott.target_variable_a") {
            config.initialConditions.grayScott.targetVariableA = decision.variableId;
            continue;
        }
        if (decision.bindingKey == "gray_scott.target_variable_b") {
            config.initialConditions.grayScott.targetVariableB = decision.variableId;
            continue;
        }
        if (decision.bindingKey == "waves.target_variable") {
            config.initialConditions.waves.targetVariable = decision.variableId;
            continue;
        }
    }
}

void configureInitializationFromCatalog(
    const ws::initialization::ModelVariableCatalog& catalog,
    ws::app::LaunchConfig& config) {
    const ws::InitialConditionType preferred =
        catalog.preferredInitializationMode.value_or(ws::InitialConditionType::Terrain);
    config.initialConditions.type = preferred;

    if (preferred == ws::InitialConditionType::Conway ||
        preferred == ws::InitialConditionType::GrayScott ||
        preferred == ws::InitialConditionType::Waves) {
        ws::initialization::InitializationRequest request;
        request.type = preferred;
        request.requireMetadataHints = false;
        const auto plan = ws::initialization::buildBindingPlan(catalog, request);
        if (!plan.hasBlockingIssues()) {
            applyBindingPlanToConfig(plan, config);
            return;
        }

        config.initialConditions.type = ws::InitialConditionType::Terrain;
    }
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
    const ws::initialization::ModelVariableCatalog& catalog,
    const std::string& patchTarget) {
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

    require(
        service.applyManualPatch(
            patchTarget,
            ws::Cell{0u, 0u},
            preferredCellPatchValue(catalog, patchTarget),
            "pipeline_manual_patch",
            message),
        "manual_patch_cell",
        message);
    require(service.step(1u, message), "step_after_manual_patch", message);

    require(
        service.applyManualPatch(
            patchTarget,
            std::nullopt,
            preferredGlobalPatchValue(catalog, patchTarget),
            "pipeline_manual_patch_global",
            message),
        "manual_patch_global",
        message);
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

void verifyProbePipeline(ws::gui::RuntimeService& service, const std::string& variable) {
    std::string message;

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

    ws::initialization::ModelVariableCatalog catalog;
    std::string catalogMessage;
    require(
        ws::initialization::loadModelVariableCatalog(modelPath, catalog, catalogMessage),
        "load_model_catalog",
        catalogMessage);
    assert(!catalog.variables.empty());

    ws::ModelExecutionSpec executionSpec;
    std::string executionMessage;
    require(
        ws::initialization::loadModelExecutionSpec(modelPath, executionSpec, executionMessage),
        "load_model_execution_spec",
        executionMessage);

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
    configureInitializationFromCatalog(catalog, config);

    std::string message;
    const std::string baseToken = sanitizeToken(modelPath.stem().string());
    const std::string worldName = "pipeline_" + baseToken + "_" + std::to_string(index);
    const std::string worldCopyName = worldName + "_copy";
    const std::string worldRenamedName = worldName + "_renamed";
    const std::string profileName = "pipeline_profile_" + baseToken + "_" + std::to_string(index);

    require(service.createWorld(worldName, config, message), "create_world", message);
    assert(service.activeWorldName() == worldName);
    assert(service.isRunning());

    ws::RuntimeCheckpoint initialCheckpoint{};
    require(service.captureCheckpoint(initialCheckpoint, message, false), "capture_initial_checkpoint", message);

    std::vector<std::string> fieldNames;
    require(service.fieldNames(fieldNames, message), "field_names", message);
    assert(!fieldNames.empty());
    const std::string representativeField = chooseRepresentativeField(fieldNames, catalog, executionSpec);
    assert(!representativeField.empty());

    auto worlds = service.listStoredWorlds(message);
    assert(hasWorld(worlds, worldName));

    require(service.step(3u, message), "initial_steps", message);

    ws::RuntimeCheckpoint evolvedCheckpoint{};
    require(service.captureCheckpoint(evolvedCheckpoint, message, false), "capture_evolved_checkpoint", message);
    const bool evolvedNaturally = snapshotsEvolved(initialCheckpoint.stateSnapshot, evolvedCheckpoint.stateSnapshot);
    const bool isConwayModel = modelPath.stem().string() == "game_of_life_model";
    if (isConwayModel) {
        require(
            fieldEvolved(initialCheckpoint.stateSnapshot, evolvedCheckpoint.stateSnapshot, "living"),
            "conway_living_evolution",
            "model=" + modelPath.string() + " field=living did not evolve after stepping");
    }

    if (!evolvedNaturally && !isConwayModel) {
        require(service.pause(message), "state_evolution_kick_pause", message);
        require(
            service.applyManualPatch(
                representativeField,
                ws::Cell{0u, 0u},
                preferredEvolutionKickValue(catalog, representativeField),
                "pipeline_evolution_kick",
                message),
            "state_evolution_kick_patch",
            message);
        require(service.resume(message), "state_evolution_kick_resume", message);
        require(service.step(2u, message), "state_evolution_kick_step", message);

        ws::RuntimeCheckpoint kickedCheckpoint{};
        require(service.captureCheckpoint(kickedCheckpoint, message, false), "capture_kicked_checkpoint", message);
        require(
            snapshotsEvolved(evolvedCheckpoint.stateSnapshot, kickedCheckpoint.stateSnapshot),
            "state_evolution",
            "model=" + modelPath.string());
    }

    verifyTimelineAndSeeking(service);
    verifyProbePipeline(service, representativeField);
    verifyParameterAndPatchPipeline(service, catalog, representativeField);
    verifyCheckpointAndDiagnostics(service);

    ws::RuntimeCheckpoint checkpoint{};
    require(service.captureCheckpoint(checkpoint, message, false), "capture_checkpoint", message);
    assert(!checkpoint.stateSnapshot.fields.empty());

    std::unordered_map<std::string, std::vector<std::string>> displayTags;
    require(service.fieldDisplayTags(displayTags, message), "field_display_tags", message);
    verifyDisplayPipeline(checkpoint, displayTags);

    require(service.summarizeField(representativeField, message), "summarize_field", message);
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
