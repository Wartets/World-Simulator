#pragma once

#include "ws/core/runtime.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::app {

// =============================================================================
// Imported Grid Data
// =============================================================================

// Grid data imported from external file formats.
struct ImportedGridData {
    std::size_t width = 0;         // Number of columns.
    std::size_t height = 0;        // Number of rows.
    std::vector<float> values;     // Flattened grid values in row-major order.
};

// =============================================================================
// Variable Domain
// =============================================================================

// Valid range for a simulation variable.
struct VariableDomain {
    float minValue = 0.0f;  // Minimum valid value.
    float maxValue = 1.0f;  // Maximum valid value.
};

// =============================================================================
// Variable Validation Stats
// =============================================================================

// Statistics from validating a variable against its domain.
struct VariableValidationStats {
    std::string variableName;          // Name of the variable.
    float minValue = 0.0f;             // Observed minimum value.
    float maxValue = 0.0f;             // Observed maximum value.
    double meanValue = 0.0;            // Mean value across the grid.
    double stdDeviation = 0.0;         // Standard deviation of values.
    std::size_t inDomainCount = 0;     // Number of values within domain.
    std::size_t outOfDomainCount = 0;  // Number of values outside domain.
};

// =============================================================================
// World Validation Result
// =============================================================================

// Result of validating a world checkpoint against domain constraints.
struct WorldValidationResult {
    bool valid = true;                   // Whether the world passes validation.
    bool hasBlockingErrors = false;      // Whether there are errors that prevent simulation.
    std::size_t warningCount = 0;        // Number of non-blocking warnings.
    std::size_t errorCount = 0;          // Number of validation errors.
    std::vector<VariableValidationStats> variableStats;  // Per-variable statistics.
    std::vector<std::string> violations; // List of validation violations.
};

// =============================================================================
// Data Importer
// =============================================================================

// Handles importing external data files into simulation grid format.
class DataImporter {
public:
    // Imports data from a CSV file.
    static bool importCsv(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    // Imports data from an image file (PNG, JPG, etc.).
    static bool importImage(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    // Imports data from a GeoTIFF file.
    static bool importGeoTiff(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    // Imports data from a NetCDF file.
    static bool importNetCdf(
        const std::filesystem::path& path,
        ImportedGridData& output,
        std::string& message);

    // Resamples imported data to a target grid size.
    static ImportedGridData resample(
        const ImportedGridData& input,
        std::size_t targetWidth,
        std::size_t targetHeight);

    // Normalizes data values to fit within a specified domain.
    static void normalizeToDomain(
        ImportedGridData& data,
        float minValue,
        float maxValue);
};

// =============================================================================
// World Validator
// =============================================================================

// Validates world checkpoints against domain constraints.
class WorldValidator {
public:
    // Validates a checkpoint against variable domain constraints.
    static WorldValidationResult validate(
        const RuntimeCheckpoint& checkpoint,
        const std::unordered_map<std::string, VariableDomain>& domains);
};

} // namespace ws::app
