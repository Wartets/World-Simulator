#include "ws/app/profile_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ws::app {

namespace {

std::string sanitizeScopeKey(std::string key) {
    key = trim(std::move(key));
    if (key.empty()) {
        return {};
    }

    for (char& ch : key) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!allowed) {
            ch = '_';
        }
    }
    return key;
}

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

std::string ProfileStore::normalizeScopeKey(std::string modelKey) {
    return sanitizeScopeKey(std::move(modelKey));
}

std::filesystem::path ProfileStore::pathFor(const std::string& profileName, const std::string& modelKey) const {
    validateProfileName(profileName);

    const auto scopedModelKey = normalizeScopeKey(modelKey);
    if (scopedModelKey.empty()) {
        return rootDirectory_ / (profileName + ".wsprofile");
    }

    const auto scopedPath = rootDirectory_ / scopedModelKey / (profileName + ".wsprofile");
    if (std::filesystem::exists(scopedPath)) {
        return scopedPath;
    }

    const auto fallbackPath = rootDirectory_ / (profileName + ".wsprofile");
    if (std::filesystem::exists(fallbackPath)) {
        return fallbackPath;
    }

    return scopedPath;
}

void ProfileStore::save(const std::string& profileName, const LaunchConfig& config, const std::string& modelKey) const {
    const auto path = pathFor(profileName, modelKey);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        throw std::runtime_error("failed to create profile directory: " + path.parent_path().string());
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
    output << "gen.type=" << initialConditionTypeToString(config.initialConditions.type) << '\n';
    output << "gen.terrain_base_frequency=" << config.initialConditions.terrain.terrainBaseFrequency << '\n';
    output << "gen.terrain_detail_frequency=" << config.initialConditions.terrain.terrainDetailFrequency << '\n';
    output << "gen.terrain_warp_strength=" << config.initialConditions.terrain.terrainWarpStrength << '\n';
    output << "gen.terrain_amplitude=" << config.initialConditions.terrain.terrainAmplitude << '\n';
    output << "gen.terrain_ridge_mix=" << config.initialConditions.terrain.terrainRidgeMix << '\n';
    output << "gen.terrain_octaves=" << config.initialConditions.terrain.terrainOctaves << '\n';
    output << "gen.terrain_lacunarity=" << config.initialConditions.terrain.terrainLacunarity << '\n';
    output << "gen.terrain_gain=" << config.initialConditions.terrain.terrainGain << '\n';
    output << "gen.sea_level=" << config.initialConditions.terrain.seaLevel << '\n';
    output << "gen.polar_cooling=" << config.initialConditions.terrain.polarCooling << '\n';
    output << "gen.latitude_banding=" << config.initialConditions.terrain.latitudeBanding << '\n';
    output << "gen.humidity_from_water=" << config.initialConditions.terrain.humidityFromWater << '\n';
    output << "gen.biome_noise_strength=" << config.initialConditions.terrain.biomeNoiseStrength << '\n';
    output << "gen.island_density=" << config.initialConditions.terrain.islandDensity << '\n';
    output << "gen.island_falloff=" << config.initialConditions.terrain.islandFalloff << '\n';
    output << "gen.coastline_sharpness=" << config.initialConditions.terrain.coastlineSharpness << '\n';
    output << "gen.archipelago_jitter=" << config.initialConditions.terrain.archipelagoJitter << '\n';
    output << "gen.erosion_strength=" << config.initialConditions.terrain.erosionStrength << '\n';
    output << "gen.shelf_depth=" << config.initialConditions.terrain.shelfDepth << '\n';
    output << "gen.conway.target_variable=" << config.initialConditions.conway.targetVariable << '\n';
    output << "gen.conway.alive_probability=" << config.initialConditions.conway.aliveProbability << '\n';
    output << "gen.conway.alive_value=" << config.initialConditions.conway.aliveValue << '\n';
    output << "gen.conway.dead_value=" << config.initialConditions.conway.deadValue << '\n';
    output << "gen.conway.smoothing_passes=" << config.initialConditions.conway.smoothingPasses << '\n';
    output << "gen.gray_scott.target_variable_a=" << config.initialConditions.grayScott.targetVariableA << '\n';
    output << "gen.gray_scott.target_variable_b=" << config.initialConditions.grayScott.targetVariableB << '\n';
    output << "gen.gray_scott.background_a=" << config.initialConditions.grayScott.backgroundA << '\n';
    output << "gen.gray_scott.background_b=" << config.initialConditions.grayScott.backgroundB << '\n';
    output << "gen.gray_scott.spot_value_a=" << config.initialConditions.grayScott.spotValueA << '\n';
    output << "gen.gray_scott.spot_value_b=" << config.initialConditions.grayScott.spotValueB << '\n';
    output << "gen.gray_scott.spot_count=" << config.initialConditions.grayScott.spotCount << '\n';
    output << "gen.gray_scott.spot_radius=" << config.initialConditions.grayScott.spotRadius << '\n';
    output << "gen.gray_scott.spot_jitter=" << config.initialConditions.grayScott.spotJitter << '\n';
    output << "gen.waves.target_variable=" << config.initialConditions.waves.targetVariable << '\n';
    output << "gen.waves.baseline=" << config.initialConditions.waves.baseline << '\n';
    output << "gen.waves.drop_amplitude=" << config.initialConditions.waves.dropAmplitude << '\n';
    output << "gen.waves.drop_radius=" << config.initialConditions.waves.dropRadius << '\n';
    output << "gen.waves.drop_count=" << config.initialConditions.waves.dropCount << '\n';
    output << "gen.waves.drop_jitter=" << config.initialConditions.waves.dropJitter << '\n';
    output << "gen.waves.ring_frequency=" << config.initialConditions.waves.ringFrequency << '\n';
}

LaunchConfig ProfileStore::load(const std::string& profileName, const std::string& modelKey) const {
    const auto path = pathFor(profileName, modelKey);
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

    auto assignOptionalString = [&](const char* key, std::string& target) {
        const auto it = kv.find(key);
        if (it != kv.end()) {
            target = it->second;
        }
    };

    auto assignOptionalInt = [&](const char* key, int& target) {
        const auto it = kv.find(key);
        if (it == kv.end()) {
            return;
        }
        const auto parsed = parseU32(it->second);
        if (parsed.has_value()) {
            target = static_cast<int>(*parsed);
        }
    };

    {
        const auto it = kv.find("gen.type");
        if (it != kv.end()) {
            const auto parsed = parseInitialConditionType(it->second);
            if (parsed.has_value()) {
                config.initialConditions.type = *parsed;
            }
        }
    }

    assignOptionalFloat("gen.terrain_base_frequency", config.initialConditions.terrain.terrainBaseFrequency);
    assignOptionalFloat("gen.terrain_detail_frequency", config.initialConditions.terrain.terrainDetailFrequency);
    assignOptionalFloat("gen.terrain_warp_strength", config.initialConditions.terrain.terrainWarpStrength);
    assignOptionalFloat("gen.terrain_amplitude", config.initialConditions.terrain.terrainAmplitude);
    assignOptionalFloat("gen.terrain_ridge_mix", config.initialConditions.terrain.terrainRidgeMix);
    {
        const auto it = kv.find("gen.terrain_octaves");
        if (it != kv.end()) {
            const auto parsed = parseU32(it->second);
            if (parsed.has_value()) {
                config.initialConditions.terrain.terrainOctaves = static_cast<int>(*parsed);
            }
        }
    }
    assignOptionalFloat("gen.terrain_lacunarity", config.initialConditions.terrain.terrainLacunarity);
    assignOptionalFloat("gen.terrain_gain", config.initialConditions.terrain.terrainGain);
    assignOptionalFloat("gen.sea_level", config.initialConditions.terrain.seaLevel);
    assignOptionalFloat("gen.polar_cooling", config.initialConditions.terrain.polarCooling);
    assignOptionalFloat("gen.latitude_banding", config.initialConditions.terrain.latitudeBanding);
    assignOptionalFloat("gen.humidity_from_water", config.initialConditions.terrain.humidityFromWater);
    assignOptionalFloat("gen.biome_noise_strength", config.initialConditions.terrain.biomeNoiseStrength);
    assignOptionalFloat("gen.island_density", config.initialConditions.terrain.islandDensity);
    assignOptionalFloat("gen.island_falloff", config.initialConditions.terrain.islandFalloff);
    assignOptionalFloat("gen.coastline_sharpness", config.initialConditions.terrain.coastlineSharpness);
    assignOptionalFloat("gen.archipelago_jitter", config.initialConditions.terrain.archipelagoJitter);
    assignOptionalFloat("gen.erosion_strength", config.initialConditions.terrain.erosionStrength);
    assignOptionalFloat("gen.shelf_depth", config.initialConditions.terrain.shelfDepth);

    assignOptionalString("gen.conway.target_variable", config.initialConditions.conway.targetVariable);
    assignOptionalFloat("gen.conway.alive_probability", config.initialConditions.conway.aliveProbability);
    assignOptionalFloat("gen.conway.alive_value", config.initialConditions.conway.aliveValue);
    assignOptionalFloat("gen.conway.dead_value", config.initialConditions.conway.deadValue);
    assignOptionalInt("gen.conway.smoothing_passes", config.initialConditions.conway.smoothingPasses);

    assignOptionalString("gen.gray_scott.target_variable_a", config.initialConditions.grayScott.targetVariableA);
    assignOptionalString("gen.gray_scott.target_variable_b", config.initialConditions.grayScott.targetVariableB);
    assignOptionalFloat("gen.gray_scott.background_a", config.initialConditions.grayScott.backgroundA);
    assignOptionalFloat("gen.gray_scott.background_b", config.initialConditions.grayScott.backgroundB);
    assignOptionalFloat("gen.gray_scott.spot_value_a", config.initialConditions.grayScott.spotValueA);
    assignOptionalFloat("gen.gray_scott.spot_value_b", config.initialConditions.grayScott.spotValueB);
    assignOptionalInt("gen.gray_scott.spot_count", config.initialConditions.grayScott.spotCount);
    assignOptionalFloat("gen.gray_scott.spot_radius", config.initialConditions.grayScott.spotRadius);
    assignOptionalFloat("gen.gray_scott.spot_jitter", config.initialConditions.grayScott.spotJitter);

    assignOptionalString("gen.waves.target_variable", config.initialConditions.waves.targetVariable);
    assignOptionalFloat("gen.waves.baseline", config.initialConditions.waves.baseline);
    assignOptionalFloat("gen.waves.drop_amplitude", config.initialConditions.waves.dropAmplitude);
    assignOptionalFloat("gen.waves.drop_radius", config.initialConditions.waves.dropRadius);
    assignOptionalInt("gen.waves.drop_count", config.initialConditions.waves.dropCount);
    assignOptionalFloat("gen.waves.drop_jitter", config.initialConditions.waves.dropJitter);
    assignOptionalFloat("gen.waves.ring_frequency", config.initialConditions.waves.ringFrequency);

    return config;
}

std::vector<std::string> ProfileStore::list(const std::string& modelKey) const {
    std::vector<std::string> names;

    const auto scopedModelKey = normalizeScopeKey(modelKey);
    const std::filesystem::path base = scopedModelKey.empty() ? rootDirectory_ : (rootDirectory_ / scopedModelKey);

    if (!std::filesystem::exists(base)) {
        if (scopedModelKey.empty()) {
            return names;
        }

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

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
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
