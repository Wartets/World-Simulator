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
    std::vector<uint32_t> dims = grid_j.value("dimensions", std::vector<uint32_t>{100, 100});
    auto fb_dims = builder.CreateVector(dims);
    auto fb_topo = builder.CreateString(grid_j.value("topology", "Cartesian2D"));
    auto fb_bc = builder.CreateString(grid_j.value("boundary_conditions", "Wrap"));
    auto fb_grid = schema::CreateGridSpec(builder, fb_dims, fb_topo, fb_bc);
    
    // numerics
    auto num_j = j.value("numerics", nlohmann::json::object());
    float dt = num_j.value("dt_ref", 0.01f);
    auto time_int = builder.CreateString(num_j.value("time_integrator", "Euler"));
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
