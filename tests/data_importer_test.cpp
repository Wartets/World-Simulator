#include "ws/app/data_importer.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

// =============================================================================
// Test Assertion Macros
// =============================================================================

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "ASSERTION FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #cond << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_EQUAL(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "EQUALITY CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #a << " != " << #b << std::endl; \
            std::cerr << "  LHS: " << (a) << ", RHS: " << (b) << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_NOT_EQUAL(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "NOT-EQUAL CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #a << " == " << #b << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_GREATER(a, b) \
    do { \
        if (!((a) > (b))) { \
            std::cerr << "GREATER CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #a << " not > " << #b << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_LESS(a, b) \
    do { \
        if (!((a) < (b))) { \
            std::cerr << "LESS CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #a << " not < " << #b << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_TRUE(cond) TEST_ASSERT(cond)
#define TEST_FALSE(cond) TEST_ASSERT(!(cond))

// =============================================================================
// Helper Functions
// =============================================================================

// Create a temporary CSV file for testing
bool createTestCsvFile(const fs::path& path, std::size_t width, std::size_t height, float startValue) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (x > 0) {
                file << ",";
            }
            file << (startValue + static_cast<float>(y * width + x) * 0.1f);
        }
        file << "\n";
    }
    return true;
}

// Create a temporary PGM file for testing
bool createTestPgmFile(const fs::path& path, std::size_t width, std::size_t height, bool binary = true) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    if (binary) {
        file << "P5\n";
    } else {
        file << "P2\n";
    }
    file << width << " " << height << "\n255\n";

    for (std::size_t i = 0; i < width * height; ++i) {
        unsigned char byte = static_cast<unsigned char>((i * 7) % 256);
        if (binary) {
            file.write(reinterpret_cast<char*>(&byte), 1);
        } else {
            file << static_cast<int>(byte);
            if (i < width * height - 1) {
                file << " ";
            }
        }
    }
    return true;
}

// =============================================================================
// Tests for CSV Import (existing functionality)
// =============================================================================

bool testCsvImportBasic() {
    fs::path testFile = fs::temp_directory_path() / "test_data.csv";
    TEST_TRUE(createTestCsvFile(testFile, 3, 2, 1.0f));

    ws::app::ImportedGridData data;
    std::string message;
    bool success = ws::app::DataImporter::importCsv(testFile, data, message);

    TEST_TRUE(success);
    TEST_EQUAL(data.width, 3ul);
    TEST_EQUAL(data.height, 2ul);
    TEST_EQUAL(data.values.size(), 6ul);

    fs::remove(testFile);
    return true;
}

bool testCsvImportDeterminism() {
    fs::path testFile = fs::temp_directory_path() / "test_determinism.csv";
    TEST_TRUE(createTestCsvFile(testFile, 4, 3, 0.5f));

    ws::app::ImportedGridData data1;
    std::string message1;
    bool success1 = ws::app::DataImporter::importCsv(testFile, data1, message1);

    ws::app::ImportedGridData data2;
    std::string message2;
    bool success2 = ws::app::DataImporter::importCsv(testFile, data2, message2);

    TEST_TRUE(success1);
    TEST_TRUE(success2);
    TEST_EQUAL(data1.width, data2.width);
    TEST_EQUAL(data1.height, data2.height);
    TEST_EQUAL(data1.values.size(), data2.values.size());

    for (std::size_t i = 0; i < data1.values.size(); ++i) {
        TEST_EQUAL(data1.values[i], data2.values[i]);
    }

    fs::remove(testFile);
    return true;
}

bool testImageImportBasic() {
    fs::path testFile = fs::temp_directory_path() / "test_image.pgm";
    TEST_TRUE(createTestPgmFile(testFile, 4, 3, true));

    ws::app::ImportedGridData data;
    std::string message;
    bool success = ws::app::DataImporter::importImage(testFile, data, message);

    TEST_TRUE(success);
    TEST_EQUAL(data.width, 4ul);
    TEST_EQUAL(data.height, 3ul);
    TEST_EQUAL(data.values.size(), 12ul);

    for (const float val : data.values) {
        TEST_GREATER(val, -0.01f);
        TEST_LESS(val, 1.01f);
    }

    fs::remove(testFile);
    return true;
}

// =============================================================================
// Tests for Feature-Flag Fallback Behavior
// =============================================================================

bool testGeoTiffFallbackMessage() {
    // This test verifies that when GeoTIFF support is not compiled in,
    // the import function returns a specific unavailable message.
    fs::path testFile = fs::temp_directory_path() / "test_fallback.tif";

    // Create a dummy file (won't be read if feature is disabled)
    std::ofstream dummy(testFile);
    dummy << "dummy";
    dummy.close();

    ws::app::ImportedGridData data;
    std::string message;
    bool success = ws::app::DataImporter::importGeoTiff(testFile, data, message);

    // If feature is disabled, expect failure and specific message
#ifdef WS_ENABLE_GEOTIFF
    // If enabled, we'd need a valid GeoTIFF file; for now, expect open failure
    TEST_FALSE(success);
    TEST_ASSERT(message.find("geotiff_import_failed") != std::string::npos || 
                 message.find("geotiff_import_unavailable") != std::string::npos);
#else
    // If disabled, expect unavailable message
    TEST_FALSE(success);
    TEST_ASSERT(message.find("geotiff_import_unavailable") != std::string::npos);
#endif

    fs::remove(testFile);
    return true;
}

bool testNetCdfFallbackMessage() {
    // This test verifies that when NetCDF support is not compiled in,
    // the import function returns a specific unavailable message.
    fs::path testFile = fs::temp_directory_path() / "test_fallback.nc";

    std::ofstream dummy(testFile);
    dummy << "dummy";
    dummy.close();

    ws::app::ImportedGridData data;
    std::string message;
    bool success = ws::app::DataImporter::importNetCdf(testFile, data, message);

    // If feature is disabled, expect failure and specific message
#ifdef WS_ENABLE_NETCDF
    // If enabled, we'd need a valid NetCDF file; for now, expect read failure
    TEST_FALSE(success);
    TEST_ASSERT(message.find("netcdf_import_failed") != std::string::npos || 
                 message.find("netcdf_import_unavailable") != std::string::npos);
#else
    // If disabled, expect unavailable message
    TEST_FALSE(success);
    TEST_ASSERT(message.find("netcdf_import_unavailable") != std::string::npos);
#endif

    fs::remove(testFile);
    return true;
}

// =============================================================================
// Tests for Resampling (existing functionality)
// =============================================================================

bool testResamplingUpscale() {
    ws::app::ImportedGridData input;
    input.width = 2;
    input.height = 2;
    input.values = {0.0f, 1.0f, 1.0f, 0.0f};

    ws::app::ImportedGridData output = ws::app::DataImporter::resample(input, 4, 4);

    TEST_EQUAL(output.width, 4ul);
    TEST_EQUAL(output.height, 4ul);
    TEST_EQUAL(output.values.size(), 16ul);

    // Bilinear interpolation can produce values outside the original range slightly
    for (float val : output.values) {
        TEST_GREATER(val, -1.0f);
        TEST_LESS(val, 2.0f);
    }

    return true;
}

bool testResamplingDownscale() {
    ws::app::ImportedGridData input;
    input.width = 4;
    input.height = 4;
    input.values.assign(16, 0.5f);

    ws::app::ImportedGridData output = ws::app::DataImporter::resample(input, 2, 2);

    TEST_EQUAL(output.width, 2ul);
    TEST_EQUAL(output.height, 2ul);
    TEST_EQUAL(output.values.size(), 4ul);

    for (float val : output.values) {
        TEST_GREATER(val, 0.4f);
        TEST_LESS(val, 0.6f);
    }

    return true;
}

// =============================================================================
// Tests for Normalization (existing functionality)
// =============================================================================

bool testNormalizationToDomain() {
    ws::app::ImportedGridData data;
    data.width = 2;
    data.height = 2;
    data.values = {0.0f, 50.0f, 25.0f, 100.0f};

    ws::app::DataImporter::normalizeToDomain(data, 0.0f, 1.0f);

    TEST_GREATER(data.values[0], -0.01f);
    TEST_LESS(data.values[0], 0.01f);

    TEST_GREATER(data.values[1], 0.45f);
    TEST_LESS(data.values[1], 0.55f);

    TEST_GREATER(data.values[3], 0.99f);
    TEST_LESS(data.values[3], 1.01f);

    return true;
}

bool testNormalizationZeroRange() {
    ws::app::ImportedGridData data;
    data.width = 2;
    data.height = 2;
    data.values = {5.0f, 5.0f, 5.0f, 5.0f};

    ws::app::DataImporter::normalizeToDomain(data, 0.2f, 0.8f);

    for (float val : data.values) {
        TEST_EQUAL(val, 0.2f);
    }

    return true;
}

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    std::cout << "Running data import tests...\n" << std::endl;

    struct TestCase {
        const char* name;
        bool (*test)();
    };

    const TestCase tests[] = {
        {"CSV Import Basic", testCsvImportBasic},
        {"CSV Import Determinism", testCsvImportDeterminism},
        {"Image Import Basic", testImageImportBasic},
        {"GeoTIFF Fallback Message", testGeoTiffFallbackMessage},
        {"NetCDF Fallback Message", testNetCdfFallbackMessage},
        {"Resampling Upscale", testResamplingUpscale},
        {"Resampling Downscale", testResamplingDownscale},
        {"Normalization To Domain", testNormalizationToDomain},
        {"Normalization Zero Range", testNormalizationZeroRange},
    };

    int passCount = 0;
    int failCount = 0;

    for (const auto& tc : tests) {
        std::cout << "  " << tc.name << "... ";
        try {
            if (tc.test()) {
                std::cout << "PASS" << std::endl;
                ++passCount;
            } else {
                std::cout << "FAIL" << std::endl;
                ++failCount;
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << std::endl;
            ++failCount;
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION" << std::endl;
            ++failCount;
        }
    }

    std::cout << "\n" << passCount << "/" << (passCount + failCount) << " tests passed" << std::endl;

    return (failCount == 0) ? 0 : 1;
}
