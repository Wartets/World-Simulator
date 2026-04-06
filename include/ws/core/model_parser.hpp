#pragma once

#include <string>
#include <memory>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "ws/core/ir_ast.hpp"

namespace ws {

// Container for all model data loaded from disk.
struct ModelContext {
    std::string metadata_json;
    std::string version_json;
    std::string model_json;
    std::string ir_logic_string;
    
    std::vector<uint8_t> flatbuffers_bin;
    std::unique_ptr<ir::Program> ir_program;
};

// Parses simulation model files from directory or ZIP archives.
class ModelParser {
public:
    // Automatically detect directory or ZIP and load
    static ModelContext load(const std::filesystem::path& path);
    
    static ModelContext loadFromDirectory(const std::filesystem::path& dirPath);
    static ModelContext loadFromZip(const std::filesystem::path& zipPath);
    
    // Translates the declarative JSON model into the compact FlatBuffers binary representation
    static std::vector<uint8_t> compileToFlatBuffers(const nlohmann::json& modelJson);
};

// Exception thrown when model parsing fails.
class ModelParseError : public std::runtime_error {
public:
    ModelParseError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace ws
