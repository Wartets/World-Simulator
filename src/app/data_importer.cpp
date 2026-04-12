#include "ws/app/data_importer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#if WS_ENABLE_GEOTIFF
#include "gdal.h"
#include "gdal_priv.h"
#endif

#if WS_ENABLE_NETCDF
#include "netcdf.hh"
#endif

namespace ws::app {
namespace {

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> result;
    std::string token;
    std::stringstream stream(line);
    while (std::getline(stream, token, ',')) {
        result.push_back(token);
    }
    return result;
}

bool parsePgmToken(std::istream& input, std::string& outToken) {
    outToken.clear();
    while (input.good()) {
        char ch = static_cast<char>(input.peek());
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            input.get();
            continue;
        }
        if (ch == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        break;
    }

    while (input.good()) {
        char ch = static_cast<char>(input.peek());
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '#') {
            break;
        }
        outToken.push_back(static_cast<char>(input.get()));
    }

    return !outToken.empty();
}

} // namespace

bool DataImporter::importCsv(const std::filesystem::path& path, ImportedGridData& output, std::string& message) {
    std::ifstream input(path);
    if (!input.is_open()) {
        message = "csv_import_failed error=open_failed";
        return false;
    }

    std::vector<float> values;
    std::size_t width = 0;
    std::size_t height = 0;
    std::string line;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto tokens = splitCsvLine(line);
        if (tokens.empty()) {
            continue;
        }

        if (width == 0) {
            width = tokens.size();
        } else if (tokens.size() != width) {
            message = "csv_import_failed error=inconsistent_row_width";
            return false;
        }

        for (const auto& token : tokens) {
            try {
                values.push_back(std::stof(token));
            } catch (...) {
                message = "csv_import_failed error=parse_error";
                return false;
            }
        }
        ++height;
    }

    if (width == 0 || height == 0 || values.size() != width * height) {
        message = "csv_import_failed error=empty_data";
        return false;
    }

    output.width = width;
    output.height = height;
    output.values = std::move(values);
    message = "csv_import_ok width=" + std::to_string(width) + " height=" + std::to_string(height);
    return true;
}

bool DataImporter::importImage(const std::filesystem::path& path, ImportedGridData& output, std::string& message) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        message = "image_import_failed error=open_failed";
        return false;
    }

    std::string magic;
    if (!parsePgmToken(input, magic)) {
        message = "image_import_failed error=header_read_failed";
        return false;
    }

    if (magic != "P2" && magic != "P5") {
        message = "image_import_failed error=unsupported_format supported=P2|P5";
        return false;
    }

    std::string token;
    if (!parsePgmToken(input, token)) {
        message = "image_import_failed error=width_missing";
        return false;
    }
    const std::size_t width = static_cast<std::size_t>(std::stoul(token));

    if (!parsePgmToken(input, token)) {
        message = "image_import_failed error=height_missing";
        return false;
    }
    const std::size_t height = static_cast<std::size_t>(std::stoul(token));

    if (!parsePgmToken(input, token)) {
        message = "image_import_failed error=max_missing";
        return false;
    }
    const float maxValue = std::max(1.0f, static_cast<float>(std::stoul(token)));

    output.width = width;
    output.height = height;
    output.values.assign(width * height, 0.0f);

    if (magic == "P2") {
        for (std::size_t i = 0; i < output.values.size(); ++i) {
            if (!parsePgmToken(input, token)) {
                message = "image_import_failed error=data_truncated";
                return false;
            }
            output.values[i] = std::stof(token) / maxValue;
        }
    } else {
        input.get();
        std::vector<unsigned char> bytes(width * height, 0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input.good()) {
            message = "image_import_failed error=data_truncated";
            return false;
        }
        for (std::size_t i = 0; i < output.values.size(); ++i) {
            output.values[i] = static_cast<float>(bytes[i]) / maxValue;
        }
    }

    message = "image_import_ok width=" + std::to_string(width) + " height=" + std::to_string(height);
    return true;
}

bool DataImporter::importGeoTiff(const std::filesystem::path& path, ImportedGridData& output, std::string& message) {
#if !WS_ENABLE_GEOTIFF
    message = "geotiff_import_unavailable reason=requires_gdal_compiled_support";
    return false;
#else
    try {
        GDALAllRegister();
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(path.string().c_str(), GA_ReadOnly));
        if (!dataset) {
            message = "geotiff_import_failed error=open_failed";
            return false;
        }

        const int rasterCount = dataset->GetRasterCount();
        if (rasterCount == 0) {
            GDALClose(dataset);
            message = "geotiff_import_failed error=no_rasters";
            return false;
        }

        const int width = dataset->GetRasterXSize();
        const int height = dataset->GetRasterYSize();
        if (width <= 0 || height <= 0) {
            GDALClose(dataset);
            message = "geotiff_import_failed error=invalid_dimensions";
            return false;
        }

        // Use first raster band
        GDALRasterBand* band = dataset->GetRasterBand(1);
        if (!band) {
            GDALClose(dataset);
            message = "geotiff_import_failed error=band_read_failed";
            return false;
        }

        output.width = static_cast<std::size_t>(width);
        output.height = static_cast<std::size_t>(height);
        output.values.assign(width * height, 0.0f);

        // Read raster data as floats
        CPLErr readErr = band->RasterIO(
            GF_Read,
            0, 0, width, height,
            output.values.data(), width, height,
            GDT_Float32, 0, 0);

        GDALClose(dataset);

        if (readErr != CE_None) {
            message = "geotiff_import_failed error=data_read_failed";
            return false;
        }

        message = "geotiff_import_ok width=" + std::to_string(width) + " height=" + std::to_string(height);
        return true;
    } catch (const std::exception& e) {
        message = std::string("geotiff_import_failed error=exception msg=") + e.what();
        return false;
    } catch (...) {
        message = "geotiff_import_failed error=unknown_exception";
        return false;
    }
#endif
}

bool DataImporter::importNetCdf(const std::filesystem::path& path, ImportedGridData& output, std::string& message) {
#if !WS_ENABLE_NETCDF
    message = "netcdf_import_unavailable reason=requires_netcdf_compiled_support";
    return false;
#else
    try {
        netCDF::NcFile datafile(path.string(), netCDF::NcFile::read);
        
        // Get all dimensions; assume 2D data with dims named x/lon and y/lat or y/x
        std::vector<netCDF::NcDim> dims = datafile.getDims();
        if (dims.size() < 2) {
            message = "netcdf_import_failed error=insufficient_dimensions";
            return false;
        }

        // Try to find x and y dimensions by name pattern
        netCDF::NcDim xDim, yDim;
        bool foundX = false, foundY = false;
        
        for (const auto& dim : dims) {
            std::string dimName = dim.getName();
            // Convert to lowercase for comparison
            std::transform(dimName.begin(), dimName.end(), dimName.begin(), ::tolower);
            
            if (!foundX && (dimName == "x" || dimName == "lon" || dimName == "longitude")) {
                xDim = dim;
                foundX = true;
            } else if (!foundY && (dimName == "y" || dimName == "lat" || dimName == "latitude")) {
                yDim = dim;
                foundY = true;
            }
        }

        if (!foundX || !foundY) {
            // Fallback: use first two dimensions
            xDim = dims[0];
            yDim = dims[1];
        }

        const int width = xDim.getSize();
        const int height = yDim.getSize();
        if (width <= 0 || height <= 0) {
            message = "netcdf_import_failed error=invalid_dimensions";
            return false;
        }

        // Find first numeric variable with matching dimensions
        std::vector<netCDF::NcVar> vars = datafile.getVars();
        netCDF::NcVar dataVar;
        bool varFound = false;
        
        for (const auto& var : vars) {
            if (var.isNull() || var.getVarType().getName() == "char" || var.getVarType().getName() == "string") {
                continue;
            }
            std::vector<netCDF::NcDim> varDims = var.getDims();
            if (varDims.size() >= 2) {
                dataVar = var;
                varFound = true;
                break;
            }
        }

        if (!varFound) {
            message = "netcdf_import_failed error=no_numeric_variable";
            return false;
        }

        output.width = static_cast<std::size_t>(width);
        output.height = static_cast<std::size_t>(height);
        output.values.assign(width * height, 0.0f);

        // Read data
        std::vector<std::size_t> start = {0, 0};
        std::vector<std::size_t> count = {static_cast<std::size_t>(height), static_cast<std::size_t>(width)};
        dataVar.getVar(start, count, output.values.data());

        message = "netcdf_import_ok width=" + std::to_string(width) + " height=" + std::to_string(height);
        return true;
    } catch (const netCDF::exceptions::NcException& e) {
        message = std::string("netcdf_import_failed error=netcdf_error msg=") + e.what();
        return false;
    } catch (const std::exception& e) {
        message = std::string("netcdf_import_failed error=exception msg=") + e.what();
        return false;
    } catch (...) {
        message = "netcdf_import_failed error=unknown_exception";
        return false;
    }
#endif
}

ImportedGridData DataImporter::resample(const ImportedGridData& input, const std::size_t targetWidth, const std::size_t targetHeight) {
    ImportedGridData output;
    output.width = targetWidth;
    output.height = targetHeight;
    output.values.assign(targetWidth * targetHeight, 0.0f);

    if (input.width == 0 || input.height == 0 || input.values.empty() || targetWidth == 0 || targetHeight == 0) {
        return output;
    }

    for (std::size_t y = 0; y < targetHeight; ++y) {
        for (std::size_t x = 0; x < targetWidth; ++x) {
            const float srcX = (static_cast<float>(x) + 0.5f) * (static_cast<float>(input.width) / static_cast<float>(targetWidth)) - 0.5f;
            const float srcY = (static_cast<float>(y) + 0.5f) * (static_cast<float>(input.height) / static_cast<float>(targetHeight)) - 0.5f;

            const std::size_t x0 = static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(srcX)), 0, static_cast<int>(input.width - 1)));
            const std::size_t y0 = static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(srcY)), 0, static_cast<int>(input.height - 1)));
            const std::size_t x1 = std::min(x0 + 1, input.width - 1);
            const std::size_t y1 = std::min(y0 + 1, input.height - 1);

            const float tx = srcX - static_cast<float>(x0);
            const float ty = srcY - static_cast<float>(y0);

            const float v00 = input.values[y0 * input.width + x0];
            const float v10 = input.values[y0 * input.width + x1];
            const float v01 = input.values[y1 * input.width + x0];
            const float v11 = input.values[y1 * input.width + x1];

            const float v0 = v00 + ((v10 - v00) * tx);
            const float v1 = v01 + ((v11 - v01) * tx);
            output.values[y * targetWidth + x] = v0 + ((v1 - v0) * ty);
        }
    }

    return output;
}

void DataImporter::normalizeToDomain(ImportedGridData& data, const float minValue, const float maxValue) {
    if (data.values.empty()) {
        return;
    }

    float srcMin = data.values.front();
    float srcMax = data.values.front();
    for (const float value : data.values) {
        srcMin = std::min(srcMin, value);
        srcMax = std::max(srcMax, value);
    }

    const float dstLo = std::min(minValue, maxValue);
    const float dstHi = std::max(minValue, maxValue);
    const float srcRange = srcMax - srcMin;
    if (srcRange <= 1e-6f) {
        std::fill(data.values.begin(), data.values.end(), dstLo);
        return;
    }

    for (float& value : data.values) {
        const float t = std::clamp((value - srcMin) / srcRange, 0.0f, 1.0f);
        value = dstLo + ((dstHi - dstLo) * t);
    }
}

WorldValidationResult WorldValidator::validate(
    const RuntimeCheckpoint& checkpoint,
    const std::unordered_map<std::string, VariableDomain>& domains) {

    WorldValidationResult result;

    for (const auto& field : checkpoint.stateSnapshot.fields) {
        if (field.values.empty()) {
            continue;
        }

        VariableValidationStats stats;
        stats.variableName = field.spec.name;
        stats.minValue = field.values.front();
        stats.maxValue = field.values.front();

        double sum = 0.0;
        for (const float value : field.values) {
            stats.minValue = std::min(stats.minValue, value);
            stats.maxValue = std::max(stats.maxValue, value);
            sum += static_cast<double>(value);
        }
        stats.meanValue = sum / static_cast<double>(field.values.size());

        double variance = 0.0;
        for (const float value : field.values) {
            const double delta = static_cast<double>(value) - stats.meanValue;
            variance += delta * delta;
        }
        stats.stdDeviation = std::sqrt(variance / static_cast<double>(field.values.size()));

        const auto domainIt = domains.find(field.spec.name);
        if (domainIt != domains.end()) {
            const float lo = std::min(domainIt->second.minValue, domainIt->second.maxValue);
            const float hi = std::max(domainIt->second.minValue, domainIt->second.maxValue);

            for (std::size_t index = 0; index < field.values.size(); ++index) {
                const float value = field.values[index];
                if (value < lo || value > hi) {
                    ++stats.outOfDomainCount;
                    if (result.violations.size() < 512) {
                        const std::size_t x = index % checkpoint.stateSnapshot.grid.width;
                        const std::size_t y = index / checkpoint.stateSnapshot.grid.width;
                        result.violations.push_back(
                            "domain_violation variable=" + field.spec.name +
                            " x=" + std::to_string(x) +
                            " y=" + std::to_string(y) +
                            " value=" + std::to_string(value) +
                            " range=[" + std::to_string(lo) + "," + std::to_string(hi) + "]");
                    }
                } else {
                    ++stats.inDomainCount;
                }
            }
        } else {
            stats.inDomainCount = field.values.size();
            ++result.warningCount;
            result.violations.push_back("domain_missing variable=" + field.spec.name);
        }

        if (stats.outOfDomainCount > 0) {
            result.valid = false;
            result.hasBlockingErrors = true;
            ++result.errorCount;
        }

        result.variableStats.push_back(std::move(stats));
    }

    return result;
}

} // namespace ws::app
