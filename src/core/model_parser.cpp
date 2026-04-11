#include "ws/core/model_parser.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>
#include <cstring>
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

// Helper to read entire file as bytes
static std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) return {};
    std::ifstream is(p, std::ios::in | std::ios::binary);
    if (!is) return {};

    is.seekg(0, std::ios::end);
    const auto size = is.tellg();
    if (size <= 0) return {};
    is.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    is.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!is) return {};
    return bytes;
}

ModelContext ModelParser::loadFromDirectory(const std::filesystem::path& dirPath) {
    ModelContext ctx;
    ctx.metadata_json = readTextFile(dirPath / "metadata.json");
    ctx.version_json = readTextFile(dirPath / "version.json");
    ctx.model_json = readTextFile(dirPath / "model.json");
    ctx.ir_logic_string = readTextFile(dirPath / "logic.ir");

    ctx.model_bin = readBinaryFile(dirPath / "model.bin");
    ctx.layout_bin = readBinaryFile(dirPath / "layout.bin");
    ctx.logic_opt_bin = readBinaryFile(dirPath / "logic.opt.bin");
    ctx.logic_cpu_bin = readBinaryFile(dirPath / "logic.cpu.bin");
    ctx.logic_gpu_spv = readBinaryFile(dirPath / "logic.gpu.spv");
    
    if (ctx.model_json.empty() && ctx.model_bin.empty()) {
        throw ModelParseError("Missing model source in directory (expected model.json or model.bin): " + dirPath.string());
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

    auto extract_bin = [&](const char* filename) -> std::vector<std::uint8_t> {
        int target_index = mz_zip_reader_locate_file(&zip_archive, filename, nullptr, 0);
        if (target_index < 0) return {};

        size_t uncomp_size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip_archive, target_index, &uncomp_size, 0);
        if (!p || uncomp_size == 0u) {
            if (p) mz_free(p);
            return {};
        }

        std::vector<std::uint8_t> bytes(uncomp_size);
        std::memcpy(bytes.data(), p, uncomp_size);
        mz_free(p);
        return bytes;
    };

    ModelContext ctx;
    ctx.metadata_json = extract_str("metadata.json");
    ctx.version_json = extract_str("version.json");
    ctx.model_json = extract_str("model.json");
    ctx.ir_logic_string = extract_str("logic.ir");
    ctx.model_bin = extract_bin("model.bin");
    ctx.layout_bin = extract_bin("layout.bin");
    ctx.logic_opt_bin = extract_bin("logic.opt.bin");
    ctx.logic_cpu_bin = extract_bin("logic.cpu.bin");
    ctx.logic_gpu_spv = extract_bin("logic.gpu.spv");

    mz_zip_reader_end(&zip_archive);

    if ((ctx.model_json.empty() && ctx.model_bin.empty()) || ctx.ir_logic_string.empty()) {
        throw ModelParseError("Required files missing from .simmodel ZIP (expected model.json or model.bin, and logic.ir): " + zipPath.string());
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
    
    // Prefer packaged binary model artifact when present.
    if (!ctx.model_bin.empty()) {
        ctx.flatbuffers_bin = ctx.model_bin;
    } else {
        // Fall back to json -> flatbuffer compilation.
        try {
            auto j = nlohmann::json::parse(ctx.model_json);
            ctx.flatbuffers_bin = compileToFlatBuffers(j);
        } catch(const std::exception& e) {
            throw ModelParseError("Failed to parse or compile model.json: " + std::string(e.what()));
        }
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
