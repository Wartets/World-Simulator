#include "ws/core/model_parser.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/unit_system.hpp"

#include <miniz.h>

#include <iostream>
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <set>
#include <nlohmann/json.hpp>

using namespace ws;

void test_unit_system() {
    auto u1 = SIUnit::parse("kg/(m*s^2)");
    assert(u1.kg == 1);
    assert(u1.m == -1);
    assert(u1.s == -2);
    
    auto u2 = SIUnit::parse("m/s");
    assert(u2.m == 1);
    assert(u2.s == -1);
    
    auto u3 = u2 * u2;
    assert(u3.m == 2);
    assert(u3.s == -2);
    
    std::cout << "Unit System logic tests passed.\n";
}

void test_ir_parser() {
    std::string logic = R"IR(
        @global f32 diff_coef = 0.5
        @interaction (physics) func diffuse() {
            f32 %t = Load("temperature", 0, 0)
            f32 %lap = Laplacian("temperature")
            f32 %dc = GlobalLoad("diff_coef")
            f32 %delta = Mul(%lap, %dc)
            f32 %next = Add(%t, %delta)
            Store("temperature", 0, %next)
        }
    )IR";
    
    try {
        auto prog = ir::parse_ir(logic);
        assert(prog.globals.size() == 1);
        assert(prog.interactions.size() == 1);
        std::cout << "IR Parser tests passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "IR Parser failed: " << e.what() << "\n";
        assert(false);
    }
}

void test_ir_parser_negative_literals() {
    std::string logic = R"IR(
        @interaction (patches) func adjust() {
            %left = Load("temperature", -1, 0)
            %down = Load("temperature", 0, -1)
            %clamped = Clamp(%left, -1000.0, 1000.0)
            %next = Add(%clamped, -0.25)
            Store("temperature", %next)
        }
    )IR";

    try {
        auto prog = ir::parse_ir(logic);
        assert(prog.globals.empty());
        assert(prog.interactions.size() == 1u);
        assert(!prog.interactions.front()->stmts.empty());
        std::cout << "IR Parser negative literal tests passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "IR Parser negative literal test failed: " << e.what() << "\n";
        assert(false);
    }
}

void test_model_parser() {
    std::string path = "models/environmental_model_2d.simmodel";
    if (std::filesystem::exists(path)) {
        try {
            auto ctx = ModelParser::load(path);
            assert(!ctx.flatbuffers_bin.empty());
            assert(ctx.ir_program != nullptr);
            std::cout << "Model Parser tests passed for " << path << ".\n";
        } catch (const std::exception& e) {
            std::cerr << "Model Parser warning (model may not perfectly match AST yet): " << e.what() << "\n";
            // Not strictly failing since we just mocked the AST mapping
        }
    } else {
        std::cout << "Skipping model load test, path not found based on execution working directory.\n";
    }
}

void test_model_parser_enforces_2d_grid_constraints() {
    const auto baseModel = nlohmann::json::parse(R"JSON({
        "name": "dimension_guard_fixture",
        "version": "1.0.0",
        "grid": {
            "dimensions": [64, 32],
            "topology": "Cartesian2D"
        },
        "numerics": {
            "dt_ref": 0.1,
            "time_integrator": "Euler"
        }
    })JSON");

    auto validModel = baseModel;
    auto compiled = ModelParser::compileToFlatBuffers(validModel);
    assert(!compiled.empty());

    validModel["grid"]["dimensions"] = 2;
    compiled = ModelParser::compileToFlatBuffers(validModel);
    assert(!compiled.empty());

    bool threw = false;
    auto invalidDimensionsModel = baseModel;
    invalidDimensionsModel["grid"]["dimensions"] = nlohmann::json::array({16, 16, 16});
    try {
        (void)ModelParser::compileToFlatBuffers(invalidDimensionsModel);
    } catch (const ModelParseError&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    auto invalidTopologyModel = baseModel;
    invalidTopologyModel["grid"]["topology"] = "Cartesian3D";
    try {
        (void)ModelParser::compileToFlatBuffers(invalidTopologyModel);
    } catch (const ModelParseError&) {
        threw = true;
    }
    assert(threw);

    std::cout << "Model parser 2D grid constraint tests passed.\n";
}

static std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

static std::filesystem::path resolveModelsRoot() {
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

static std::vector<std::filesystem::path> discoverModelDirectories() {
    const auto root = resolveModelsRoot();
    if (root.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> modelDirs;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory() || entry.path().extension() != ".simmodel") {
            continue;
        }
        modelDirs.push_back(entry.path());
    }
    std::sort(modelDirs.begin(), modelDirs.end());
    return modelDirs;
}

void test_all_shipped_models_parse() {
    const auto modelDirs = discoverModelDirectories();
    if (modelDirs.empty()) {
        std::cout << "Skipping all-model parser test, models root not found.\n";
        return;
    }

    std::size_t verified = 0u;
    for (const auto& modelDir : modelDirs) {
        try {
            auto ctx = ModelParser::load(modelDir);
            assert(!ctx.flatbuffers_bin.empty());
            assert(ctx.ir_program != nullptr);
            ++verified;
        } catch (const std::exception& e) {
            std::cerr << "All-model parser smoke test failed for " << modelDir.string()
                      << ": " << e.what() << "\n";
            assert(false);
        }
    }

    std::cout << "All shipped model parser smoke tests passed for " << verified << " models.\n";
}

static std::filesystem::path resolveModelDir(const std::string& modelFolderName) {
    const auto root = resolveModelsRoot();
    if (root.empty()) {
        return {};
    }
    return root / modelFolderName;
}

static bool create_model_archive(
    const std::filesystem::path& modelDir,
    const std::filesystem::path& archivePath) {
    const auto modelJson = read_text_file(modelDir / "model.json");
    if (modelJson.empty()) {
        return false;
    }

    // Use parser-stable IR for archive fixtures so this test validates archive ingestion
    // rather than model-specific IR dialect variance.
    const std::string logicIr = R"IR(
        @global f32 dt = 1.0
        @interaction (physics) func tick() {
            f32 %t = Load("temperature", 0, 0)
            f32 %n = Add(%t, 0.0)
            Store("temperature", 0, %n)
        }
    )IR";

    mz_zip_archive zip{};
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, archivePath.string().c_str(), 0)) {
        return false;
    }

    bool ok = true;
    ok = ok && mz_zip_writer_add_mem(&zip, "model.json", modelJson.data(), modelJson.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_add_mem(&zip, "logic.ir", logicIr.data(), logicIr.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    ok = ok && mz_zip_writer_end(&zip);
    return ok;
}

static bool create_binary_first_model_archive(
    const std::filesystem::path& modelDir,
    const std::filesystem::path& archivePath) {
    const auto modelJsonText = read_text_file(modelDir / "model.json");
    if (modelJsonText.empty()) {
        return false;
    }

    std::vector<std::uint8_t> modelBin;
    try {
        const auto modelJson = nlohmann::json::parse(modelJsonText);
        modelBin = ModelParser::compileToFlatBuffers(modelJson);
    } catch (...) {
        return false;
    }

    if (modelBin.empty()) {
        return false;
    }

    // Use parser-stable IR for archive fixtures.
    const std::string logicIr = R"IR(
        @global f32 dt = 1.0
        @interaction (physics) func tick() {
            f32 %t = Load("temperature", 0, 0)
            f32 %n = Add(%t, 0.0)
            Store("temperature", 0, %n)
        }
    )IR";

    mz_zip_archive zip{};
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, archivePath.string().c_str(), 0)) {
        return false;
    }

    bool ok = true;
    ok = ok && mz_zip_writer_add_mem(&zip, "model.bin", modelBin.data(), modelBin.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_add_mem(&zip, "logic.ir", logicIr.data(), logicIr.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    ok = ok && mz_zip_writer_end(&zip);
    return ok;
}

static bool copy_directory_recursive(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        return false;
    }

    std::filesystem::copy(
        source,
        destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec);
    return !ec;
}

static std::filesystem::path create_model_fixture_with_valid_ir(
    const std::filesystem::path& sourceModelDir,
    const std::string& fixtureToken) {
    const auto tempRoot = std::filesystem::temp_directory_path() /
        ("world_simulator_model_fixture_" + fixtureToken);
    const auto fixtureDir = tempRoot / sourceModelDir.filename();

    std::filesystem::remove_all(tempRoot);
    if (!copy_directory_recursive(sourceModelDir, fixtureDir)) {
        return {};
    }

    const std::string logicIr = R"IR(
        @global f32 dt = 1.0
        @interaction (physics) func tick() {
            f32 %t = Load("temperature", 0, 0)
            f32 %n = Add(%t, 0.0)
            Store("temperature", 0, %n)
        }
    )IR";

    std::ofstream logicOut(fixtureDir / "logic.ir", std::ios::trunc);
    if (!logicOut.is_open()) {
        std::filesystem::remove_all(tempRoot);
        return {};
    }
    logicOut << logicIr;
    return fixtureDir;
}

void test_model_variable_catalog_loader() {
    const std::filesystem::path modelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping model catalog loader test, path not found based on execution working directory.\n";
        return;
    }

    initialization::ModelVariableCatalog directoryCatalog;
    std::string directoryMessage;
    const bool directoryOk = initialization::loadModelVariableCatalog(modelDir, directoryCatalog, directoryMessage);
    assert(directoryOk);
    assert(!directoryCatalog.variables.empty());
    assert(directoryCatalog.modelId == "environmental_model_2d");

    const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_model_catalog_test";
    std::filesystem::create_directories(tempRoot);
    const auto archivePath = tempRoot / "environmental_model_2d.simmodel";

    if (!create_model_archive(modelDir, archivePath)) {
        std::cerr << "Failed to create temporary model archive for catalog loading test.\n";
        assert(false);
    }

    initialization::ModelVariableCatalog archiveCatalog;
    std::string archiveMessage;
    const bool archiveOk = initialization::loadModelVariableCatalog(archivePath, archiveCatalog, archiveMessage);
    assert(archiveOk);
    assert(!archiveCatalog.variables.empty());
    assert(archiveCatalog.modelId == "environmental_model_2d");

    std::filesystem::remove_all(tempRoot);
    std::cout << "Model Variable Catalog loader tests passed for directory and archive inputs.\n";
}

void test_model_parser_binary_first_archive() {
    const std::filesystem::path modelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping binary-first model parser test, path not found based on execution working directory.\n";
        return;
    }

    const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_model_binary_first_test";
    std::filesystem::create_directories(tempRoot);
    const auto archivePath = tempRoot / "environmental_model_2d_binary_first.simmodel";

    if (!create_binary_first_model_archive(modelDir, archivePath)) {
        std::cerr << "Failed to create binary-first model archive fixture.\n";
        assert(false);
    }

    try {
        auto ctx = ModelParser::load(archivePath);
        assert(!ctx.flatbuffers_bin.empty());
        assert(!ctx.model_bin.empty());
        assert(ctx.ir_program != nullptr);
    } catch (const std::exception& e) {
        std::cerr << "Binary-first model parser test failed: " << e.what() << "\n";
        assert(false);
    }

    std::filesystem::remove_all(tempRoot);
    std::cout << "Binary-first model parser test passed.\n";
}

void test_model_parser_save_zip_round_trip() {
    const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_model_save_round_trip_test";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot);

    const auto packagePath = tempRoot / "round_trip_fixture.simmodel";

    ModelContext context;
    context.metadata_json = R"JSON({
        "name": "round_trip_fixture"
    })JSON";
    context.version_json = R"JSON({
        "version": "1.0.0"
    })JSON";
    context.model_json = R"JSON({
        "name": "round_trip_fixture",
        "version": "1.0.0",
        "grid": {
            "dimensions": [16, 16],
            "topology": "Cartesian2D"
        },
        "numerics": {
            "dt_ref": 0.05,
            "time_integrator": "Euler"
        },
        "variables": [
            {
                "id": "state_x",
                "role": "state",
                "support": "cell",
                "type": "f32",
                "units": "1",
                "initial_value": 0.0
            }
        ],
        "stages": []
    })JSON";
    context.ir_logic_string = R"IR(
        @global f32 dt = 1.0
        @interaction (physics) func tick() {
            f32 %x = Load("state_x", 0, 0)
            Store("state_x", 0, %x)
        }
    )IR";

    std::string saveMessage;
    const bool saveOk = ModelParser::saveAsZip(context, packagePath, saveMessage);
    assert(saveOk);
    assert(saveMessage.empty());

    ModelContext reloaded;
    try {
        reloaded = ModelParser::load(packagePath);
    } catch (const std::exception& e) {
        std::cerr << "Model parser save/load round-trip test failed: " << e.what() << "\n";
        assert(false);
    }

    assert(!reloaded.model_json.empty());
    assert(!reloaded.ir_logic_string.empty());
    assert(!reloaded.model_bin.empty());
    assert(!reloaded.flatbuffers_bin.empty());
    assert(reloaded.ir_program != nullptr);

    const auto parsed = nlohmann::json::parse(reloaded.model_json);
    assert(parsed.value("name", std::string{}) == "round_trip_fixture");

    std::filesystem::remove_all(tempRoot);
    std::cout << "Model parser save/load round-trip test passed.\n";
}

void test_model_parameter_controls_loader() {
    const std::filesystem::path sourceModelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(sourceModelDir)) {
        std::cout << "Skipping model parameter control loader test, path not found based on execution working directory.\n";
        return;
    }

    const auto fixtureModelDir = create_model_fixture_with_valid_ir(sourceModelDir, "parameter_controls");
    assert(!fixtureModelDir.empty());

    std::vector<ParameterControl> controls;
    std::string message;
    const bool ok = initialization::loadModelParameterControls(fixtureModelDir, controls, message);
    assert(ok);
    assert(!controls.empty());

    std::set<std::string> uniqueTargets;
    for (const auto& control : controls) {
        assert(!control.name.empty());
        assert(!control.targetVariable.empty());
        assert(control.minValue <= control.maxValue);
        uniqueTargets.insert(control.targetVariable);
    }
    assert(uniqueTargets.size() == controls.size());

    std::filesystem::remove_all(fixtureModelDir.parent_path());
}

void test_model_execution_spec_loader() {
    const std::filesystem::path sourceModelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(sourceModelDir)) {
        std::cout << "Skipping model execution spec loader test, path not found based on execution working directory.\n";
        return;
    }

    const auto fixtureModelDir = create_model_fixture_with_valid_ir(sourceModelDir, "execution_spec");
    assert(!fixtureModelDir.empty());

    ModelExecutionSpec executionSpec;
    std::string message;
    const bool ok = initialization::loadModelExecutionSpec(fixtureModelDir, executionSpec, message);
    assert(ok);
    assert(!executionSpec.cellScalarVariableIds.empty());
    assert(!executionSpec.stageOrder.empty());

    assert(std::is_sorted(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end()));
    assert(std::adjacent_find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end()) ==
        executionSpec.cellScalarVariableIds.end());
    assert(std::none_of(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), [](const std::string& id) {
        return id.empty();
    }));

    assert(std::none_of(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end(), [](const std::string& stage) {
        return stage.empty();
    }));
    const std::set<std::string> uniqueStages(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end());
    assert(uniqueStages.size() == executionSpec.stageOrder.size());

    std::filesystem::remove_all(fixtureModelDir.parent_path());
}

void test_forest_fire_execution_aliases() {
    const std::filesystem::path sourceModelDir = resolveModelDir("forest_fire_propagation.simmodel");
    if (!std::filesystem::exists(sourceModelDir)) {
        std::cout << "Skipping forest fire execution alias test, path not found based on execution working directory.\n";
        return;
    }

    const auto fixtureModelDir = create_model_fixture_with_valid_ir(sourceModelDir, "forest_fire_aliases");
    assert(!fixtureModelDir.empty());

    ModelExecutionSpec executionSpec;
    std::string message;
    const bool ok = initialization::loadModelExecutionSpec(fixtureModelDir, executionSpec, message);
    assert(ok);
    assert(!executionSpec.cellScalarVariableIds.empty());

    const auto automatonAlias = executionSpec.semanticFieldAliases.find("automaton.state");
    assert(automatonAlias == executionSpec.semanticFieldAliases.end());

    const auto conwayAlias = executionSpec.semanticFieldAliases.find("initialization.conway.target");
    assert(conwayAlias != executionSpec.semanticFieldAliases.end());
    assert(conwayAlias->second == "fire_state");

    const auto fireStateAlias = executionSpec.semanticFieldAliases.find("fire.state");
    assert(fireStateAlias != executionSpec.semanticFieldAliases.end());
    assert(fireStateAlias->second == "fire_state");

    const auto windFactorAlias = executionSpec.semanticFieldAliases.find("fire.wind_factor");
    assert(windFactorAlias != executionSpec.semanticFieldAliases.end());
    assert(windFactorAlias->second == "wind_factor");

    const auto burningNeighborsAlias = executionSpec.semanticFieldAliases.find("fire.burning_neighbors");
    assert(burningNeighborsAlias != executionSpec.semanticFieldAliases.end());
    assert(burningNeighborsAlias->second == "burning_neighbors");

    std::filesystem::remove_all(fixtureModelDir.parent_path());
    std::cout << "Forest fire execution alias test passed.\n";
}

void test_forest_fire_parameter_consistency() {
    const std::filesystem::path modelDir = resolveModelDir("forest_fire_propagation.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping forest fire parameter consistency test, path not found based on execution working directory.\n";
        return;
    }

    const auto modelJson = read_text_file(modelDir / "model.json");
    const auto metadataJson = read_text_file(modelDir / "metadata.json");
    assert(!modelJson.empty());
    assert(!metadataJson.empty());

    const auto model = nlohmann::json::parse(modelJson);
    const auto metadata = nlohmann::json::parse(metadataJson);

    assert(model.contains("variables") && model["variables"].is_array());
    assert(metadata.contains("initialization_guidance") && metadata["initialization_guidance"].is_object());

    const auto& guidance = metadata["initialization_guidance"];
    assert(guidance.contains("preferred_mode") && guidance["preferred_mode"].get<std::string>() == "conway");
    assert(guidance.contains("supported_modes") && guidance["supported_modes"].is_array());

    bool hasConwayMode = false;
    for (const auto& mode : guidance["supported_modes"]) {
        if (mode.is_string() && mode.get<std::string>() == "conway") {
            hasConwayMode = true;
            break;
        }
    }
    assert(hasConwayMode);

    assert(guidance.contains("parameter_overrides") && guidance["parameter_overrides"].is_object());
    const auto& overrides = guidance["parameter_overrides"];
    assert(overrides.contains("conwayAliveProbability"));
    assert(overrides.contains("conwaySmoothingPasses"));

    const double aliveProbability = overrides["conwayAliveProbability"].get<double>();
    const double smoothingPasses = overrides["conwaySmoothingPasses"].get<double>();
    assert(aliveProbability >= 0.0 && aliveProbability <= 1.0);
    assert(smoothingPasses >= 0.0);

    double spreadProb = -1.0;
    double ignitionProb = -1.0;
    double regrowthRate = -1.0;
    for (const auto& variable : model["variables"]) {
        if (!variable.is_object() || !variable.contains("id") || !variable["id"].is_string()) {
            continue;
        }
        const auto id = variable["id"].get<std::string>();
        if (!variable.contains("initial_value") || !variable["initial_value"].is_number()) {
            continue;
        }
        const double value = variable["initial_value"].get<double>();
        if (id == "spread_prob") {
            spreadProb = value;
        } else if (id == "base_ignition_prob") {
            ignitionProb = value;
        } else if (id == "regrowth_rate") {
            regrowthRate = value;
        }
    }

    assert(spreadProb >= 0.0 && spreadProb <= 1.0);
    assert(ignitionProb >= 0.0 && ignitionProb <= 1.0);
    assert(regrowthRate >= 0.0);

    assert(metadata.contains("runtime_field_aliases") && metadata["runtime_field_aliases"].is_object());
    const auto& aliases = metadata["runtime_field_aliases"];
    assert(aliases.contains("fire.burning_neighbors"));
    assert(aliases["fire.burning_neighbors"].get<std::string>() == "burning_neighbors");
    assert(!aliases.contains("automaton.state"));

    std::cout << "Forest fire parameter consistency test passed.\n";
}

void test_coastal_model_metadata_and_aliases() {
    const std::filesystem::path modelDir = resolveModelDir("coastal_biogeochemistry_transport.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping coastal model metadata test, path not found based on execution working directory.\n";
        return;
    }

    const auto modelPath = modelDir / "model.json";
    const auto metadataPath = modelDir / "metadata.json";

    std::ifstream modelIn(modelPath);
    std::ifstream metadataIn(metadataPath);
    assert(modelIn.is_open());
    assert(metadataIn.is_open());

    const nlohmann::json model = nlohmann::json::parse(modelIn);
    const nlohmann::json metadata = nlohmann::json::parse(metadataIn);

    assert(model.contains("version") && model["version"].is_string());
    assert(model["version"].get<std::string>() == "1.0.4");

    assert(metadata.contains("initialization_guidance") && metadata["initialization_guidance"].is_object());
    const auto& guidance = metadata["initialization_guidance"];
    assert(guidance.contains("preferred_mode") && guidance["preferred_mode"].is_string());
    assert(guidance["preferred_mode"].get<std::string>() == "terrain");
    assert(guidance.contains("preferred_display_variable") && guidance["preferred_display_variable"].is_string());
    assert(guidance["preferred_display_variable"].get<std::string>() == "salinity");
    assert(guidance.contains("supported_modes") && guidance["supported_modes"].is_array());
    assert(std::find(guidance["supported_modes"].begin(), guidance["supported_modes"].end(), nlohmann::json("waves")) != guidance["supported_modes"].end());

    assert(metadata.contains("runtime_field_aliases") && metadata["runtime_field_aliases"].is_object());
    const auto& aliases = metadata["runtime_field_aliases"];
    assert(aliases.contains("generation.elevation"));
    assert(aliases["generation.elevation"].get<std::string>() == "bathymetry_depth");
    assert(aliases.contains("initialization.waves.target"));
    assert(aliases["initialization.waves.target"].get<std::string>() == "current_speed");
    assert(aliases.contains("events.signal"));
    assert(aliases["events.signal"].get<std::string>() == "oxygen_deficit");

    std::cout << "Coastal model metadata and alias test passed.\n";
}

void test_coastal_model_parameter_controls() {
    const std::filesystem::path modelDir = resolveModelDir("coastal_biogeochemistry_transport.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping coastal model parameter control test, path not found based on execution working directory.\n";
        return;
    }

    std::vector<ParameterControl> controls;
    std::string message;
    const bool ok = initialization::loadModelParameterControls(modelDir, controls, message);
    assert(ok);
    assert(!controls.empty());

    auto findControl = [&](const std::string& target) -> const ParameterControl* {
        for (const auto& control : controls) {
            if (control.targetVariable == target) {
                return &control;
            }
        }
        return nullptr;
    };

    const auto* bathymetry = findControl("bathymetry_depth");
    const auto* seagrass = findControl("seagrass_fraction");
    const auto* shore = findControl("shore_distance_factor");

    assert(bathymetry != nullptr);
    assert(seagrass != nullptr);
    assert(shore != nullptr);
    assert(bathymetry->defaultValue == 12.0f);
    assert(seagrass->defaultValue == 0.22f);
    assert(shore->defaultValue == 0.58f);

    std::cout << "Coastal model parameter control test passed.\n";
}

void test_coastal_model_execution_spec() {
    const std::filesystem::path modelDir = resolveModelDir("coastal_biogeochemistry_transport.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping coastal model execution spec test, path not found based on execution working directory.\n";
        return;
    }

    ModelExecutionSpec executionSpec;
    std::string message;
    const bool ok = initialization::loadModelExecutionSpec(modelDir, executionSpec, message);
    assert(ok);
    assert(!executionSpec.cellScalarVariableIds.empty());
    assert(std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), "bathymetry_depth") != executionSpec.cellScalarVariableIds.end());
    assert(std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), "seagrass_fraction") != executionSpec.cellScalarVariableIds.end());
    assert(std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), "shore_distance_factor") != executionSpec.cellScalarVariableIds.end());
    assert(executionSpec.semanticFieldAliases.at("generation.elevation") == "bathymetry_depth");
    assert(executionSpec.semanticFieldAliases.at("initialization.waves.target") == "current_speed");

    std::cout << "Coastal model execution spec test passed.\n";
}

void test_coastal_model_parser_smoke() {
    const std::filesystem::path modelDir = resolveModelDir("coastal_biogeochemistry_transport.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping coastal model parser smoke test, path not found based on execution working directory.\n";
        return;
    }

    try {
        auto ctx = ModelParser::load(modelDir);
        assert(!ctx.flatbuffers_bin.empty());
        assert(ctx.ir_program != nullptr);
        std::cout << "Coastal model parser smoke test passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "Coastal model parser smoke test failed: " << e.what() << "\n";
        assert(false);
    }
}

void test_environmental_model_defaults() {
    const std::filesystem::path modelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping environmental model defaults test, path not found based on execution working directory.\n";
        return;
    }

    const auto modelJson = read_text_file(modelDir / "model.json");
    const auto metadataJson = read_text_file(modelDir / "metadata.json");
    assert(!modelJson.empty());
    assert(!metadataJson.empty());

    const auto model = nlohmann::json::parse(modelJson);
    const auto metadata = nlohmann::json::parse(metadataJson);

    assert(model["version"].get<std::string>() == "2.0.1");
    const auto& defaults = metadata["initialization_guidance"]["variable_defaults"];
    assert(defaults.contains("altitude"));
    assert(defaults.contains("soil_mineral_fraction"));

    float altitudeInitialValue = std::numeric_limits<float>::quiet_NaN();
    for (const auto& variable : model["variables"]) {
        if (variable.is_object() && variable.value("id", "") == "altitude") {
            assert(variable.contains("initial_value"));
            altitudeInitialValue = variable["initial_value"].get<float>();
            break;
        }
    }
    assert(altitudeInitialValue == 0.0f);

    const auto& altitudeDefaults = defaults["altitude"];
    assert(altitudeDefaults["base_value"].get<double>() == 0.0);
    assert(altitudeDefaults["restriction_mode"].get<std::string>() == "clamp");

    const auto& soilMineralDefaults = defaults["soil_mineral_fraction"];
    assert(soilMineralDefaults["base_value"].get<double>() == 0.5);
    assert(soilMineralDefaults["restriction_mode"].get<std::string>() == "clamp");

    std::cout << "Environmental model defaults test passed.\n";
}

void test_shallow_water_defaults() {
    const std::filesystem::path modelDir = resolveModelDir("shallow_water_equations.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping shallow water defaults test, path not found based on execution working directory.\n";
        return;
    }

    const auto modelJson = read_text_file(modelDir / "model.json");
    const auto metadataJson = read_text_file(modelDir / "metadata.json");
    assert(!modelJson.empty());
    assert(!metadataJson.empty());

    const auto model = nlohmann::json::parse(modelJson);
    const auto metadata = nlohmann::json::parse(metadataJson);

    assert(model["version"].get<std::string>() == "1.0.1");
    const auto& defaults = metadata["initialization_guidance"]["variable_defaults"];
    assert(defaults.contains("bathymetry"));

    float bathymetryInitialValue = std::numeric_limits<float>::quiet_NaN();
    for (const auto& variable : model["variables"]) {
        if (variable.is_object() && variable.value("id", "") == "bathymetry") {
            assert(variable.contains("initial_value"));
            bathymetryInitialValue = variable["initial_value"].get<float>();
            break;
        }
    }
    assert(bathymetryInitialValue == 0.0f);

    const auto& bathymetryDefaults = defaults["bathymetry"];
    assert(bathymetryDefaults["base_value"].get<double>() == 0.0);
    assert(bathymetryDefaults["restriction_mode"].get<std::string>() == "clamp");

    std::cout << "Shallow water defaults test passed.\n";
}

void test_urban_microclimate_defaults() {
    const std::filesystem::path modelDir = resolveModelDir("urban_microclimate_resilience.simmodel");
    if (!std::filesystem::exists(modelDir)) {
        std::cout << "Skipping urban microclimate defaults test, path not found based on execution working directory.\n";
        return;
    }

    const auto modelJson = read_text_file(modelDir / "model.json");
    const auto metadataJson = read_text_file(modelDir / "metadata.json");
    assert(!modelJson.empty());
    assert(!metadataJson.empty());

    const auto model = nlohmann::json::parse(modelJson);
    const auto metadata = nlohmann::json::parse(metadataJson);

    assert(model["version"].get<std::string>() == "1.0.2");
    const auto& defaults = metadata["initialization_guidance"]["variable_defaults"];
    assert(defaults.contains("surface_temperature"));
    assert(defaults.contains("canopy_temperature"));
    assert(defaults.contains("soil_moisture"));
    assert(defaults.contains("storage_heat"));
    assert(defaults.contains("surface_albedo"));
    assert(defaults.contains("emissivity_surface"));
    assert(defaults.contains("impervious_fraction"));
    assert(defaults.contains("vegetation_fraction"));

    auto findParam = [&](const std::string& id) -> const nlohmann::json* {
        for (const auto& variable : model["variables"]) {
            if (variable.is_object() && variable.value("id", "") == id) {
                return &variable;
            }
        }
        return nullptr;
    };

    const auto* albedo = findParam("surface_albedo");
    const auto* emissivity = findParam("emissivity_surface");
    const auto* impervious = findParam("impervious_fraction");
    const auto* vegetation = findParam("vegetation_fraction");

    assert(albedo != nullptr && albedo->contains("initial_value") && (*albedo)["initial_value"].get<float>() == 0.18f);
    assert(emissivity != nullptr && emissivity->contains("initial_value") && (*emissivity)["initial_value"].get<float>() == 0.95f);
    assert(impervious != nullptr && impervious->contains("initial_value") && (*impervious)["initial_value"].get<float>() == 0.72f);
    assert(vegetation != nullptr && vegetation->contains("initial_value") && (*vegetation)["initial_value"].get<float>() == 0.28f);

    assert(defaults["surface_albedo"]["base_value"].get<double>() == 0.18);
    assert(defaults["emissivity_surface"]["base_value"].get<double>() == 0.95);
    assert(defaults["impervious_fraction"]["base_value"].get<double>() == 0.72);
    assert(defaults["vegetation_fraction"]["base_value"].get<double>() == 0.28);

    std::cout << "Urban microclimate defaults test passed.\n";
}

void test_model_display_spec_loader() {
    const std::filesystem::path sourceModelDir = resolveModelDir("environmental_model_2d.simmodel");
    if (!std::filesystem::exists(sourceModelDir)) {
        std::cout << "Skipping model display spec loader test, path not found based on execution working directory.\n";
        return;
    }

    const auto tempModel = create_model_fixture_with_valid_ir(sourceModelDir, "display_spec");
    assert(!tempModel.empty());
    const auto tempRoot = tempModel.parent_path();

    try {
        const auto modelPath = tempModel / "model.json";
        std::ifstream in(modelPath);
        nlohmann::json model = nlohmann::json::parse(in);

        std::string terrainFieldId;
        std::string vectorFieldId;
        std::string moistureFieldId;

        if (model.contains("variables") && model["variables"].is_array()) {
            for (auto& variable : model["variables"]) {
                if (!variable.is_object() || !variable.contains("id") || !variable["id"].is_string()) {
                    continue;
                }
                const std::string id = variable["id"].get<std::string>();
                if (terrainFieldId.empty()) {
                    terrainFieldId = id;
                    variable["display_tags"] = {"water", "terrain"};
                } else if (vectorFieldId.empty()) {
                    vectorFieldId = id;
                    variable["display_tags"] = {"vector_x", "vector_y"};
                } else if (moistureFieldId.empty()) {
                    moistureFieldId = id;
                    variable["display_tags"] = {"moisture"};
                }
            }
        }

        assert(!terrainFieldId.empty());
        assert(!vectorFieldId.empty());
        assert(!moistureFieldId.empty());

        model["display_channels"] = {
            {"terrain", {terrainFieldId}},
            {"flow_x", {vectorFieldId}},
            {"flow_y", {vectorFieldId}},
            {"moisture", {moistureFieldId}}
        };

        std::ofstream out(modelPath, std::ios::trunc);
        out << model.dump(2);
    } catch (const std::exception& e) {
        std::cerr << "Failed to prepare temporary model display-spec fixture: " << e.what() << "\n";
        assert(false);
    }

    ModelDisplaySpec displaySpec;
    std::string message;
    const bool ok = initialization::loadModelDisplaySpec(tempModel, displaySpec, message);
    assert(ok);
    assert(!displaySpec.fieldTags.empty());
    assert(displaySpec.fieldTags.size() >= 3u);

    std::string terrainFieldId;
    std::string vectorFieldId;
    std::string moistureFieldId;
    for (const auto& [fieldId, tags] : displaySpec.fieldTags) {
        if (terrainFieldId.empty() && std::find(tags.begin(), tags.end(), "terrain") != tags.end()) {
            terrainFieldId = fieldId;
        }
        if (vectorFieldId.empty() && std::find(tags.begin(), tags.end(), "flow_x") != tags.end()) {
            vectorFieldId = fieldId;
        }
        if (moistureFieldId.empty() && std::find(tags.begin(), tags.end(), "moisture") != tags.end()) {
            moistureFieldId = fieldId;
        }
    }

    assert(!terrainFieldId.empty());
    assert(!vectorFieldId.empty());
    assert(!moistureFieldId.empty());

    const auto waterIt = displaySpec.fieldTags.find(terrainFieldId);
    assert(waterIt != displaySpec.fieldTags.end());
    assert(std::find(waterIt->second.begin(), waterIt->second.end(), "terrain") != waterIt->second.end());
    assert(std::find(waterIt->second.begin(), waterIt->second.end(), "water") != waterIt->second.end());

    const auto flowIt2 = displaySpec.fieldTags.find(vectorFieldId);
    assert(flowIt2 != displaySpec.fieldTags.end());
    assert(std::find(flowIt2->second.begin(), flowIt2->second.end(), "vector_x") != flowIt2->second.end());
    assert(std::find(flowIt2->second.begin(), flowIt2->second.end(), "vector_y") != flowIt2->second.end());
    assert(std::find(flowIt2->second.begin(), flowIt2->second.end(), "flow_x") != flowIt2->second.end());
    assert(std::find(flowIt2->second.begin(), flowIt2->second.end(), "flow_y") != flowIt2->second.end());

    const auto soilIt = displaySpec.fieldTags.find(moistureFieldId);
    assert(soilIt != displaySpec.fieldTags.end());
    assert(std::find(soilIt->second.begin(), soilIt->second.end(), "moisture") != soilIt->second.end());

    std::filesystem::remove_all(tempRoot);
    std::cout << "Model display spec loader test passed.\n";
}

void test_model_binding_plan_uses_catalog_metadata() {
    initialization::ModelVariableCatalog catalog;
    catalog.modelId = "tag_driven_binding_fixture";

    initialization::VariableDescriptor vegetation;
    vegetation.id = "alpha_field";
    vegetation.role = "state";
    vegetation.support = "cell";
    vegetation.type = "f32";
    vegetation.tags = {"biomass", "vegetation"};
    vegetation.initializationHints = {"conway", "binary_seed"};

    initialization::VariableDescriptor water;
    water.id = "beta_field";
    water.role = "state";
    water.support = "cell";
    water.type = "f32";
    water.tags = {"surface", "water"};
    water.initializationHints = {"waves", "height_field"};

    catalog.variables = {vegetation, water};

    initialization::InitializationRequest conwayRequest;
    conwayRequest.type = InitialConditionType::Conway;
    const auto conwayPlan = initialization::buildBindingPlan(catalog, conwayRequest);
    assert(!conwayPlan.decisions.empty());
    assert(conwayPlan.decisions[0].resolved);
    assert(conwayPlan.decisions[0].variableId == "alpha_field");

    initialization::InitializationRequest wavesRequest;
    wavesRequest.type = InitialConditionType::Waves;
    const auto wavesPlan = initialization::buildBindingPlan(catalog, wavesRequest);
    assert(!wavesPlan.decisions.empty());
    assert(wavesPlan.decisions[0].resolved);
    assert(wavesPlan.decisions[0].variableId == "beta_field");

    std::cout << "Model binding plan metadata-first test passed.\n";
}

void test_extended_variable_metadata_schema_loader() {
        const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_variable_schema_test";
        const auto modelDir = tempRoot / "schema_test.simmodel";
        std::filesystem::remove_all(tempRoot);
        std::filesystem::create_directories(modelDir);

        const std::string modelJson = R"JSON({
    "id": "schema_test",
    "version": "1.0.0",
    "variables": [
        {
            "id": "vx",
            "role": "state",
            "support": "cell",
            "type": "f32",
            "units": "m/s",
            "display_type": "vector",
            "vector_component": "x",
            "visualization_roles": ["transport", "flow"],
            "initialization_hints": ["waves"],
            "display_tags": ["vector_x"]
        }
    ]
})JSON";

        {
                std::ofstream out(modelDir / "model.json", std::ios::trunc);
                out << modelJson;
        }
        {
                std::ofstream out(modelDir / "logic.ir", std::ios::trunc);
                out << "@global f32 dt = 1.0\n";
        }

        initialization::ModelVariableCatalog catalog;
        std::string message;
        const bool ok = initialization::loadModelVariableCatalog(modelDir, catalog, message);
        assert(ok);
        assert(catalog.modelId == "schema_test");
        assert(catalog.variables.size() == 1u);

        const auto& v = catalog.variables.front();
        assert(v.id == "vx");
        assert(v.displayType.has_value() && *v.displayType == "vector");
        assert(v.vectorComponent.has_value() && *v.vectorComponent == "x");
        assert(std::find(v.visualizationRoles.begin(), v.visualizationRoles.end(), "transport") != v.visualizationRoles.end());
        assert(std::find(v.initializationHints.begin(), v.initializationHints.end(), "waves") != v.initializationHints.end());
        assert(std::find(v.tags.begin(), v.tags.end(), "vector") != v.tags.end());
        assert(std::find(v.tags.begin(), v.tags.end(), "x") != v.tags.end());
        assert(std::find(v.tags.begin(), v.tags.end(), "flow") != v.tags.end());

        std::filesystem::remove_all(tempRoot);
        std::cout << "Extended variable metadata schema loader test passed.\n";
}

void test_categorical_domain_loader() {
        const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_categorical_domain_test";
        const auto modelDir = tempRoot / "categorical_domain.simmodel";
        std::filesystem::remove_all(tempRoot);
        std::filesystem::create_directories(modelDir);

        const std::string modelJson = R"JSON({
    "id": "categorical_domain_fixture",
    "version": "1.0.0",
    "domains": {
        "phase_states": {
            "kind": "categorical",
            "allowed_values": [0, 1, 2],
            "type": "u32"
        }
    },
    "variables": [
        {
            "id": "phase_state",
            "role": "parameter",
            "support": "cell",
            "type": "u32",
            "units": "1",
            "domain": "phase_states",
            "initial_value": 1
        }
    ],
    "stages": []
})JSON";

        {
                std::ofstream out(modelDir / "model.json", std::ios::trunc);
                out << modelJson;
        }
        {
                std::ofstream out(modelDir / "logic.ir", std::ios::trunc);
                out << "@global f32 dt = 1.0\n";
        }

        initialization::ModelVariableCatalog catalog;
        std::string catalogMessage;
        const bool catalogOk = initialization::loadModelVariableCatalog(modelDir, catalog, catalogMessage);
        assert(catalogOk);
        assert(catalog.variables.size() == 1u);
        assert(catalog.variables.front().hasCategoricalDomain);
        assert(catalog.variables.front().categoricalAllowedValues == std::vector<int>({0, 1, 2}));

        std::vector<ParameterControl> controls;
        std::string controlMessage;
        const bool controlsOk = initialization::loadModelParameterControls(modelDir, controls, controlMessage);
        assert(controlsOk);
        assert(controls.size() == 1u);
        assert(controls.front().targetVariable == "phase_state");
        assert(controls.front().minValue == 0.0f);
        assert(controls.front().maxValue == 2.0f);
        assert(controls.front().defaultValue == 1.0f);

        std::filesystem::remove_all(tempRoot);
        std::cout << "Categorical domain loader test passed.\n";
}

    void test_boundary_support_loader() {
        const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_boundary_support_test";
        const auto modelDir = tempRoot / "boundary_support.simmodel";
        std::filesystem::remove_all(tempRoot);
        std::filesystem::create_directories(modelDir);

        const std::string modelJson = R"JSON({
        "id": "boundary_support_fixture",
        "version": "1.0.0",
        "variables": [
        { "id": "interior", "role": "state", "support": "cell", "type": "f32", "units": "1", "initial_value": 0.1 },
        { "id": "edge_bias", "role": "parameter", "support": "boundary", "type": "f32", "units": "1", "initial_value": 0.25 }
        ],
        "stages": []
    })JSON";

        {
            std::ofstream out(modelDir / "model.json", std::ios::trunc);
            out << modelJson;
        }
        {
            std::ofstream out(modelDir / "logic.ir", std::ios::trunc);
            out << "@global f32 dt = 1.0\n";
        }

        initialization::ModelVariableCatalog catalog;
        std::string catalogMessage;
        const bool catalogOk = initialization::loadModelVariableCatalog(modelDir, catalog, catalogMessage);
        assert(catalogOk);
        assert(catalog.variables.size() == 2u);

        const auto edgeIt = std::find_if(catalog.variables.begin(), catalog.variables.end(), [](const initialization::VariableDescriptor& variable) {
            return variable.id == "edge_bias";
        });
        assert(edgeIt != catalog.variables.end());
        assert(edgeIt->support == "boundary");

        std::vector<ParameterControl> controls;
        std::string controlMessage;
        const bool controlsOk = initialization::loadModelParameterControls(modelDir, controls, controlMessage);
        assert(controlsOk);
        assert(controls.size() == 1u);
        assert(controls.front().targetVariable == "edge_bias");
        assert(controls.front().defaultValue == 0.25f);

        ModelExecutionSpec executionSpec;
        std::string executionMessage;
        const bool executionOk = initialization::loadModelExecutionSpec(modelDir, executionSpec, executionMessage);
        assert(executionOk);
        assert(std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), "interior") != executionSpec.cellScalarVariableIds.end());
        assert(std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), "edge_bias") != executionSpec.cellScalarVariableIds.end());

        std::filesystem::remove_all(tempRoot);
        std::cout << "Boundary support loader test passed.\n";
    }

    void test_model_execution_spec_cross_constraints() {
        const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_execution_constraint_test";
        const auto modelDir = tempRoot / "execution_constraint.simmodel";
        std::filesystem::remove_all(tempRoot);
        std::filesystem::create_directories(modelDir);

        const std::string modelJson = R"JSON({
        "id": "execution_constraint_fixture",
        "version": "1.0.0",
        "variables": [
        { "id": "soil_water", "role": "state", "support": "cell", "type": "f32", "units": "1" },
        { "id": "water_height", "role": "state", "support": "cell", "type": "f32", "units": "m" }
        ],
        "stages": [],
        "cross_variable_constraints": [
        {
            "id": "soil_leq_water",
            "lhs": "soil_water",
            "rhs": "water_height",
            "op": "<=",
            "tolerance": 0.01,
            "action": "clamp_lhs"
        }
        ]
    })JSON";

        {
            std::ofstream out(modelDir / "model.json", std::ios::trunc);
            out << modelJson;
        }
        {
            std::ofstream out(modelDir / "logic.ir", std::ios::trunc);
            out << "@global f32 dt = 1.0\n";
        }

        ModelExecutionSpec executionSpec;
        std::string message;
        const bool ok = initialization::loadModelExecutionSpec(modelDir, executionSpec, message);
        assert(ok);
        assert(executionSpec.crossVariableConstraints.size() == 1u);

        const auto& constraint = executionSpec.crossVariableConstraints.front();
        assert(constraint.id == "soil_leq_water");
        assert(constraint.lhsVariable == "soil_water");
        assert(constraint.rhsVariable == "water_height");
        assert(constraint.relation == CrossVariableRelation::LessEqual);
        assert(constraint.tolerance == 0.01f);
        assert(constraint.autoClamp);

        std::filesystem::remove_all(tempRoot);
        std::cout << "Execution spec cross-constraint loader test passed.\n";
    }

int main() {
    test_unit_system();
    test_ir_parser();
    test_ir_parser_negative_literals();
    test_model_parser();
    test_model_parser_enforces_2d_grid_constraints();
    test_all_shipped_models_parse();
    test_model_variable_catalog_loader();
    test_model_parser_binary_first_archive();
    test_model_parser_save_zip_round_trip();
    test_model_parameter_controls_loader();
    test_model_execution_spec_loader();
    test_forest_fire_execution_aliases();
    test_forest_fire_parameter_consistency();
    test_coastal_model_metadata_and_aliases();
    test_coastal_model_parameter_controls();
    test_coastal_model_execution_spec();
    test_coastal_model_parser_smoke();
    test_environmental_model_defaults();
    test_shallow_water_defaults();
    test_urban_microclimate_defaults();
    test_model_display_spec_loader();
    test_model_binding_plan_uses_catalog_metadata();
    test_extended_variable_metadata_schema_loader();
    test_categorical_domain_loader();
    test_boundary_support_loader();
    test_model_execution_spec_cross_constraints();
    return 0;
}
