#pragma once

#include "ws/core/runtime.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::app {

struct ImportedGridData {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<float> values;
};

struct VariableDomain {
    float minValue = 0.0f;
    float maxValue = 1.0f;
};

struct VariableValidationStats {
    std::string variableName;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double meanValue = 0.0;
    double stdDeviation = 0.0;
    std::size_t inDomainCount = 0;
    std::size_t outOfDomainCount = 0;
};

struct WorldValidationResult {
    bool valid = true;
    bool hasBlockingErrors = false;
    std::size_t warningCount = 0;
    std::size_t errorCount = 0;
    std::vector<VariableValidationStats> variableStats;
    std::vector<std::string> violations;
};

class DataImporter {
public:
    static bool importCsv(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    static bool importImage(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    static bool importGeoTiff(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    static bool importNetCdf(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    static ImportedGridData resample(
        const ImportedGridData& input,
        std::size_t targetWidth,
        std::size_t targetHeight);

    static void normalizeToDomain(
        ImportedGridData& data,
        float minValue,
        float maxValue);
};

class WorldValidator {
public:
    static WorldValidationResult validate(
        const RuntimeCheckpoint& checkpoint,
        const std::unordered_map<std::string, VariableDomain>& domains);
};

} // namespace ws::app
