#include "ws/core/model_parser.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include "miniz.h"
#include "model_schema_generated.h"
#include <stdexcept>

namespace ws {

// Helper to read entire file
static std::string readTextFile(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) return "";
    std::ifstream is(p, std::ios::in | std::ios::binary);
    if (!is) return "";
    std::ostringstream ss;
    ss << is.rdbuf();
    return ss.str();
}

ModelContext ModelParser::loadFromDirectory(const std::filesystem::path& dirPath) {
    ModelContext ctx;
    ctx.metadata_json = readTextFile(dirPath / "metadata.json");
    ctx.version_json = readTextFile(dirPath / "version.json");
    ctx.model_json = readTextFile(dirPath / "model.json");
    ctx.ir_logic_string = readTextFile(dirPath / "logic.ir");
    
    if (ctx.model_json.empty()) {
        throw ModelParseError("Missing or empty model.json in directory: " + dirPath.string());
    }
    if (ctx.ir_logic_string.empty()) {
        throw ModelParseError("Missing or empty logic.ir in directory: " + dirPath.string());
    }
    
    return ctx;
}

ModelContext ModelParser::loadFromZip(const std::filesystem::path& zipPath) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, zipPath.string().c_str(), 0)) {
        throw ModelParseError("Failed to open ZIP archive: " + zipPath.string());
    }

    auto extract_str = [&](const char* filename) -> std::string {
        int target_index = mz_zip_reader_locate_file(&zip_archive, filename, nullptr, 0);
        if (target_index < 0) return "";
        
        size_t uncomp_size;
        void* p = mz_zip_reader_extract_to_heap(&zip_archive, target_index, &uncomp_size, 0);
        if (!p) return "";
        
        std::string s((const char*)p, uncomp_size);
        mz_free(p);
        return s;
    };

    ModelContext ctx;
    ctx.metadata_json = extract_str("metadata.json");
    ctx.version_json = extract_str("version.json");
    ctx.model_json = extract_str("model.json");
    ctx.ir_logic_string = extract_str("logic.ir");

    mz_zip_reader_end(&zip_archive);

    if (ctx.model_json.empty() || ctx.ir_logic_string.empty()) {
        throw ModelParseError("Required files missing from .simmodel ZIP: " + zipPath.string());
    }

    return ctx;
}

ModelContext ModelParser::load(const std::filesystem::path& path) {
    ModelContext ctx;
    if (std::filesystem::is_directory(path)) {
        ctx = loadFromDirectory(path);
    } else {
        ctx = loadFromZip(path);
    }

    // Try parsing IR logic immediately to fail fast
    try {
        ctx.ir_program = std::make_unique<ir::Program>(ir::parse_ir(ctx.ir_logic_string));
    } catch (const std::exception& e) {
        throw ModelParseError("Failed to parse logic.ir: " + std::string(e.what()));
    }
    
    // Attempt json to flatbuffer compilation dynamically
    try {
        auto j = nlohmann::json::parse(ctx.model_json);
        ctx.flatbuffers_bin = compileToFlatBuffers(j);
    } catch(const std::exception& e) {
        throw ModelParseError("Failed to parse or compile model.json: " + std::string(e.what()));
    }

    return ctx;
}

std::vector<uint8_t> ModelParser::compileToFlatBuffers(const nlohmann::json& j) {
    flatbuffers::FlatBufferBuilder builder(1024);
    
    // grid
    auto grid_j = j.value("grid", nlohmann::json::object());
    std::vector<uint32_t> dims{100, 100};
    if (grid_j.contains("dimensions")) {
        const auto& d = grid_j["dimensions"];
        if (d.is_array()) {
            try {
                dims = d.get<std::vector<uint32_t>>();
            } catch (...) {
                dims = {100, 100};
            }
        } else if (d.is_number_integer() || d.is_number_unsigned()) {
            const auto dim = d.get<uint32_t>();
            if (dim <= 1u) {
                dims = {100, 100};
            } else {
                dims = std::vector<uint32_t>(dim, 100u);
            }
        }
    }

    auto fb_dims = builder.CreateVector(dims);
    const std::string topology = grid_j.value("topology", std::string{"Cartesian2D"});

    std::string boundaryConditions{"Wrap"};
    if (grid_j.contains("boundary_conditions")) {
        const auto& bc = grid_j["boundary_conditions"];
        if (bc.is_string()) {
            boundaryConditions = bc.get<std::string>();
        } else if (bc.is_object()) {
            if (bc.contains("x") && bc.contains("y") && bc["x"].is_string() && bc["y"].is_string()) {
                boundaryConditions = bc["x"].get<std::string>() + "," + bc["y"].get<std::string>();
            }
        }
    }

    auto fb_topo = builder.CreateString(topology);
    auto fb_bc = builder.CreateString(boundaryConditions);
    auto fb_grid = schema::CreateGridSpec(builder, fb_dims, fb_topo, fb_bc);
    
    // numerics
    auto num_j = j.value("numerics", nlohmann::json::object());
    float dt = 0.01f;
    if (num_j.contains("dt_ref")) {
        const auto& dtRef = num_j["dt_ref"];
        if (dtRef.is_number_float() || dtRef.is_number_integer()) {
            dt = dtRef.get<float>();
        }
    }

    const std::string timeIntegrator = num_j.value("time_integrator", std::string{"Euler"});
    auto time_int = builder.CreateString(timeIntegrator);
    std::vector<std::string> sp_scheme_str = num_j.value("spatial_schemes", std::vector<std::string>{});
    std::vector<flatbuffers::Offset<flatbuffers::String>> fb_sp_vec;
    for(auto& s : sp_scheme_str) fb_sp_vec.push_back(builder.CreateString(s));
    auto fb_num = schema::CreateNumericsSpec(builder, dt, time_int, builder.CreateVector(fb_sp_vec));

    // build top
    auto fb_name = builder.CreateString(j.value("name", "Unnamed"));
    auto fb_version = builder.CreateString(j.value("version", "1.0"));
    
    schema::ModelBuilder model_builder(builder);
    model_builder.add_name(fb_name);
    model_builder.add_version(fb_version);
    model_builder.add_grid(fb_grid);
    model_builder.add_numerics(fb_num);
    
    auto root = model_builder.Finish();
    builder.Finish(root);

    uint8_t *buf = builder.GetBufferPointer();
    int size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

} // namespace ws
