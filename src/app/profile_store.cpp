#include "ws/app/profile_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ws::app {

namespace {

void validateProfileName(const std::string& profileName) {
    if (profileName.empty()) {
        throw std::invalid_argument("profile name must not be empty");
    }
    for (const char ch : profileName) {
        const bool valid =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!valid) {
            throw std::invalid_argument("profile name contains invalid characters");
        }
    }
}

} // namespace

ProfileStore::ProfileStore(std::filesystem::path rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {}

std::filesystem::path ProfileStore::pathFor(const std::string& profileName) const {
    validateProfileName(profileName);
    return rootDirectory_ / (profileName + ".wsprofile");
}

void ProfileStore::save(const std::string& profileName, const LaunchConfig& config) const {
    const auto path = pathFor(profileName);

    std::error_code ec;
    std::filesystem::create_directories(rootDirectory_, ec);
    if (ec) {
        throw std::runtime_error("failed to create profile directory: " + rootDirectory_.string());
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("failed to open profile for write: " + path.string());
    }

    output << "seed=" << config.seed << '\n';
    output << "width=" << config.grid.width << '\n';
    output << "height=" << config.grid.height << '\n';
    output << "tier=" << toString(config.tier) << '\n';
    output << "temporal=" << temporalPolicyToString(config.temporalPolicy) << '\n';
    output << "gen.terrain_base_frequency=" << config.worldGen.terrainBaseFrequency << '\n';
    output << "gen.terrain_detail_frequency=" << config.worldGen.terrainDetailFrequency << '\n';
    output << "gen.terrain_warp_strength=" << config.worldGen.terrainWarpStrength << '\n';
    output << "gen.terrain_amplitude=" << config.worldGen.terrainAmplitude << '\n';
    output << "gen.terrain_ridge_mix=" << config.worldGen.terrainRidgeMix << '\n';
    output << "gen.terrain_octaves=" << config.worldGen.terrainOctaves << '\n';
    output << "gen.terrain_lacunarity=" << config.worldGen.terrainLacunarity << '\n';
    output << "gen.terrain_gain=" << config.worldGen.terrainGain << '\n';
    output << "gen.sea_level=" << config.worldGen.seaLevel << '\n';
    output << "gen.polar_cooling=" << config.worldGen.polarCooling << '\n';
    output << "gen.latitude_banding=" << config.worldGen.latitudeBanding << '\n';
    output << "gen.humidity_from_water=" << config.worldGen.humidityFromWater << '\n';
    output << "gen.biome_noise_strength=" << config.worldGen.biomeNoiseStrength << '\n';
    output << "gen.island_density=" << config.worldGen.islandDensity << '\n';
    output << "gen.island_falloff=" << config.worldGen.islandFalloff << '\n';
    output << "gen.coastline_sharpness=" << config.worldGen.coastlineSharpness << '\n';
    output << "gen.archipelago_jitter=" << config.worldGen.archipelagoJitter << '\n';
    output << "gen.erosion_strength=" << config.worldGen.erosionStrength << '\n';
    output << "gen.shelf_depth=" << config.worldGen.shelfDepth << '\n';
}

LaunchConfig ProfileStore::load(const std::string& profileName) const {
    const auto path = pathFor(profileName);
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open profile for read: " + path.string());
    }

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const auto splitAt = line.find('=');
        if (splitAt == std::string::npos || splitAt == 0 || splitAt == line.size() - 1) {
            throw std::runtime_error("invalid profile line: " + line);
        }

        auto key = trim(line.substr(0, splitAt));
        auto value = trim(line.substr(splitAt + 1));
        kv[key] = value;
    }

    LaunchConfig config;

    const auto seedIt = kv.find("seed");
    const auto widthIt = kv.find("width");
    const auto heightIt = kv.find("height");
    const auto tierIt = kv.find("tier");
    const auto temporalIt = kv.find("temporal");

    if (seedIt == kv.end() || widthIt == kv.end() || heightIt == kv.end() || tierIt == kv.end() || temporalIt == kv.end()) {
        throw std::runtime_error("profile file missing required keys");
    }

    const auto seed = parseU64(seedIt->second);
    const auto width = parseU32(widthIt->second);
    const auto height = parseU32(heightIt->second);
    const auto tier = parseTier(tierIt->second);
    const auto temporal = parseTemporalPolicy(temporalIt->second);

    if (!seed.has_value() || !width.has_value() || !height.has_value() || !tier.has_value() || !temporal.has_value()) {
        throw std::runtime_error("profile file has invalid value types");
    }

    config.seed = *seed;
    config.grid = GridSpec{*width, *height};
    config.tier = *tier;
    config.temporalPolicy = *temporal;

    auto assignOptionalFloat = [&](const char* key, float& target) {
        const auto it = kv.find(key);
        if (it == kv.end()) {
            return;
        }
        const auto parsed = parseFloat(it->second);
        if (parsed.has_value()) {
            target = *parsed;
        }
    };

    assignOptionalFloat("gen.terrain_base_frequency", config.worldGen.terrainBaseFrequency);
    assignOptionalFloat("gen.terrain_detail_frequency", config.worldGen.terrainDetailFrequency);
    assignOptionalFloat("gen.terrain_warp_strength", config.worldGen.terrainWarpStrength);
    assignOptionalFloat("gen.terrain_amplitude", config.worldGen.terrainAmplitude);
    assignOptionalFloat("gen.terrain_ridge_mix", config.worldGen.terrainRidgeMix);
    {
        const auto it = kv.find("gen.terrain_octaves");
        if (it != kv.end()) {
            const auto parsed = parseU32(it->second);
            if (parsed.has_value()) {
                config.worldGen.terrainOctaves = static_cast<int>(*parsed);
            }
        }
    }
    assignOptionalFloat("gen.terrain_lacunarity", config.worldGen.terrainLacunarity);
    assignOptionalFloat("gen.terrain_gain", config.worldGen.terrainGain);
    assignOptionalFloat("gen.sea_level", config.worldGen.seaLevel);
    assignOptionalFloat("gen.polar_cooling", config.worldGen.polarCooling);
    assignOptionalFloat("gen.latitude_banding", config.worldGen.latitudeBanding);
    assignOptionalFloat("gen.humidity_from_water", config.worldGen.humidityFromWater);
    assignOptionalFloat("gen.biome_noise_strength", config.worldGen.biomeNoiseStrength);
    assignOptionalFloat("gen.island_density", config.worldGen.islandDensity);
    assignOptionalFloat("gen.island_falloff", config.worldGen.islandFalloff);
    assignOptionalFloat("gen.coastline_sharpness", config.worldGen.coastlineSharpness);
    assignOptionalFloat("gen.archipelago_jitter", config.worldGen.archipelagoJitter);
    assignOptionalFloat("gen.erosion_strength", config.worldGen.erosionStrength);
    assignOptionalFloat("gen.shelf_depth", config.worldGen.shelfDepth);

    return config;
}

std::vector<std::string> ProfileStore::list() const {
    std::vector<std::string> names;

    if (!std::filesystem::exists(rootDirectory_)) {
        return names;
    }

    for (const auto& entry : std::filesystem::directory_iterator(rootDirectory_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto extension = entry.path().extension().string();
        if (extension != ".wsprofile") {
            continue;
        }

        names.push_back(entry.path().stem().string());
    }

    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ws::app
