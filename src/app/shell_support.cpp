#include "ws/app/shell_support.hpp"
#include "ws/core/subsystems/subsystems.hpp"
#include "ws/core/time_integrator.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <limits>

namespace ws::app {

namespace {

std::filesystem::path resolveWorkspaceRoot() {
    std::error_code ec;
    std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) {
        return std::filesystem::path{"."};
    }

    for (std::filesystem::path probe = current; !probe.empty(); probe = probe.parent_path()) {
        if (std::filesystem::exists(probe / "CMakeLists.txt")) {
            return probe;
        }
        if (probe == probe.parent_path()) {
            break;
        }
    }

    return current;
}

std::filesystem::path resolveModelsRoot(const std::filesystem::path& modelsRoot) {
    std::error_code ec;
    if (std::filesystem::exists(modelsRoot, ec)) {
        return modelsRoot;
    }

    const auto workspaceRoot = resolveWorkspaceRoot();
    const auto workspaceModels = workspaceRoot / modelsRoot;
    if (std::filesystem::exists(workspaceModels, ec)) {
        return workspaceModels;
    }

    return modelsRoot;
}

} // namespace

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    auto isSpace = [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string temporalPolicyToString(const TemporalPolicy policy) {
    switch (policy) {
        case TemporalPolicy::UniformA: return "uniform";
        case TemporalPolicy::PhasedB: return "phased";
        case TemporalPolicy::MultiRateC: return "multirate";
    }
    return "unknown";
}

std::optional<TemporalPolicy> parseTemporalPolicy(const std::string& token) {
    const std::string normalized = toLower(token);
    if (normalized == "uniform" || normalized == "a") {
        return TemporalPolicy::UniformA;
    }
    if (normalized == "phased" || normalized == "b") {
        return TemporalPolicy::PhasedB;
    }
    if (normalized == "multirate" || normalized == "c") {
        return TemporalPolicy::MultiRateC;
    }
    return std::nullopt;
}

std::string initialConditionTypeToString(const InitialConditionType type) {
    switch (type) {
        case InitialConditionType::Terrain: return "terrain";
        case InitialConditionType::Conway: return "conway";
        case InitialConditionType::GrayScott: return "gray_scott";
        case InitialConditionType::Waves: return "waves";
        case InitialConditionType::Blank: return "blank";
        case InitialConditionType::Voronoi: return "voronoi";
        case InitialConditionType::Clustering: return "clustering";
        case InitialConditionType::SparseRandom: return "sparse_random";
        case InitialConditionType::GradientField: return "gradient_field";
        case InitialConditionType::Checkerboard: return "checkerboard";
        case InitialConditionType::RadialPattern: return "radial_pattern";
        case InitialConditionType::MultiScale: return "multiscale";
        case InitialConditionType::DiffusionLimit: return "diffusion_limit";
    }
    return "terrain";
}

std::optional<InitialConditionType> parseInitialConditionType(const std::string& token) {
    const std::string normalized = toLower(token);
    if (normalized == "terrain" || normalized == "geographic") {
        return InitialConditionType::Terrain;
    }
    if (normalized == "conway" || normalized == "life" || normalized == "game_of_life") {
        return InitialConditionType::Conway;
    }
    if (normalized == "gray_scott" || normalized == "grayscott" || normalized == "gray-scott") {
        return InitialConditionType::GrayScott;
    }
    if (normalized == "waves" || normalized == "wave") {
        return InitialConditionType::Waves;
    }
    if (normalized == "blank" || normalized == "zero") {
        return InitialConditionType::Blank;
    }
    if (normalized == "voronoi" || normalized == "tessellation") {
        return InitialConditionType::Voronoi;
    }
    if (normalized == "clustering" || normalized == "clusters") {
        return InitialConditionType::Clustering;
    }
    if (normalized == "sparse_random" || normalized == "sparse" || normalized == "random") {
        return InitialConditionType::SparseRandom;
    }
    if (normalized == "gradient_field" || normalized == "gradient") {
        return InitialConditionType::GradientField;
    }
    if (normalized == "checkerboard" || normalized == "checker") {
        return InitialConditionType::Checkerboard;
    }
    if (normalized == "radial_pattern" || normalized == "radial" || normalized == "concentric") {
        return InitialConditionType::RadialPattern;
    }
    if (normalized == "multiscale" || normalized == "multi_scale") {
        return InitialConditionType::MultiScale;
    }
    if (normalized == "diffusion_limit" || normalized == "dla") {
        return InitialConditionType::DiffusionLimit;
    }
    return std::nullopt;
}

std::optional<ModelTier> parseTier(const std::string& token) {
    const std::string normalized = toLower(token);
    if (normalized == "minimal" || normalized == "0") {
        return ModelTier::Minimal;
    }
    if (normalized == "standard" || normalized == "1") {
        return ModelTier::Standard;
    }
    if (normalized == "advanced" || normalized == "2") {
        return ModelTier::Advanced;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> parseU64(const std::string& token) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(token, &consumed, 10);
        if (consumed != token.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> parseU32(const std::string& token) {
    const auto value = parseU64(token);
    if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::optional<float> parseFloat(const std::string& token) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stof(token, &consumed);
        if (consumed != token.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::string normalizeTimeIntegratorId(std::string token) {
    token = trim(std::move(token));
    for (char& ch : token) {
        if (ch == ' ' || ch == '-' || ch == '/') {
            ch = '_';
        }
    }
    return toLower(std::move(token));
}

std::optional<std::string> resolveTimeIntegratorId(const std::string& token) {
    const std::string normalized = normalizeTimeIntegratorId(token);
    if (normalized.empty()) {
        return std::nullopt;
    }

    const auto& registry = TimeIntegratorRegistry::instance();
    return registry.resolveCanonicalId(normalized);
}

std::string normalizeModelKey(std::string value) {
    value = trim(std::move(value));
    if (value.empty()) {
        return {};
    }

    std::filesystem::path tokenPath(value);
    std::string normalized = tokenPath.stem().string();
    if (normalized.empty()) {
        normalized = tokenPath.filename().string();
    }

    for (char& ch : normalized) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!allowed) {
            ch = '_';
        }
    }

    return normalized;
}

std::vector<ModelCatalogEntry> listAvailableModels(const std::filesystem::path& modelsRoot) {
    std::vector<ModelCatalogEntry> entries;
    std::error_code ec;
    const std::filesystem::path discoveredRoot = resolveModelsRoot(modelsRoot);
    if (!std::filesystem::exists(discoveredRoot, ec)) {
        return entries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(discoveredRoot, ec)) {
        if (ec) {
            break;
        }

        const auto& path = entry.path();
        if ((entry.is_directory() || entry.is_regular_file()) && path.extension() == ".simmodel") {
            ModelCatalogEntry catalogEntry;
            catalogEntry.key = normalizeModelKey(path.stem().string());
            catalogEntry.path = path;
            catalogEntry.isDirectory = entry.is_directory();
            if (!catalogEntry.key.empty()) {
                entries.push_back(std::move(catalogEntry));
            }
        }
    }

    std::sort(entries.begin(), entries.end(), [](const ModelCatalogEntry& lhs, const ModelCatalogEntry& rhs) {
        if (lhs.key != rhs.key) {
            return lhs.key < rhs.key;
        }
        return lhs.path.string() < rhs.path.string();
    });

    return entries;
}

ProfileResolverInput buildProfileInput(const ModelTier tier) {
    ProfileResolverInput input;
    for (const auto& subsystem : makePhase4Subsystems()) {
        if (!subsystem) {
            continue;
        }
        input.requestedSubsystemTiers[subsystem->name()] = tier;
    }
    input.compatibilityAssumptions = {
        "interactive_shell",
        "runtime_manual_control"
    };
    input.conservedVariables = {
        "surface_water_w",
        "resource_stock_r"
    };
    return input;
}

RuntimeConfig makeRuntimeConfig(const LaunchConfig& launchConfig) {
    RuntimeConfig config;
    config.seed = launchConfig.seed;
    config.grid = launchConfig.grid;
    config.boundaryMode = BoundaryMode::Clamp;
    config.unitRegime = UnitRegime::Normalized;
    config.temporalPolicy = launchConfig.temporalPolicy;
    if (const auto resolvedIntegratorId = resolveTimeIntegratorId(launchConfig.timeIntegratorId); resolvedIntegratorId.has_value()) {
        config.timeIntegratorId = *resolvedIntegratorId;
    } else {
        config.timeIntegratorId = "explicit_euler";
    }
    config.profileInput = buildProfileInput(launchConfig.tier);
    config.initialConditions = launchConfig.initialConditions;
    return config;
}

const std::vector<LaunchPreset>& allPresets() {
    static const std::vector<LaunchPreset> presets = {
        {"baseline", LaunchConfig{42, GridSpec{128, 128}, ModelTier::Minimal, TemporalPolicy::UniformA}, "Balanced default deterministic setup (square high-context grid)"},
        {"phased_b", LaunchConfig{777, GridSpec{160, 160}, ModelTier::Standard, TemporalPolicy::PhasedB}, "Intermediate coupling with phased policy"},
        {"dense_c", LaunchConfig{2026, GridSpec{192, 192}, ModelTier::Advanced, TemporalPolicy::MultiRateC}, "Advanced coupling stress-oriented profile"},
        {"conway", [] {
            LaunchConfig cfg{31415, GridSpec{128, 128}, ModelTier::Minimal, TemporalPolicy::UniformA};
            cfg.initialConditions.type = InitialConditionType::Conway;
            cfg.initialConditions.conway.targetVariable = "vegetation_v";
            cfg.initialConditions.conway.aliveProbability = 0.35f;
            cfg.initialConditions.conway.aliveValue = 1.0f;
            cfg.initialConditions.conway.deadValue = 0.0f;
            cfg.initialConditions.conway.smoothingPasses = 2;
            return cfg;
        }(), "Conway-style random binary seeding on vegetation_v with smoothing"},
        {"gray_scott", [] {
            LaunchConfig cfg{27182, GridSpec{192, 192}, ModelTier::Minimal, TemporalPolicy::UniformA};
            cfg.initialConditions.type = InitialConditionType::GrayScott;
            cfg.initialConditions.grayScott.targetVariableA = "resource_stock_r";
            cfg.initialConditions.grayScott.targetVariableB = "vegetation_v";
            cfg.initialConditions.grayScott.backgroundA = 1.0f;
            cfg.initialConditions.grayScott.backgroundB = 0.0f;
            cfg.initialConditions.grayScott.spotValueA = 0.0f;
            cfg.initialConditions.grayScott.spotValueB = 1.0f;
            cfg.initialConditions.grayScott.spotCount = 6;
            cfg.initialConditions.grayScott.spotRadius = 12.0f;
            cfg.initialConditions.grayScott.spotJitter = 0.45f;
            return cfg;
        }(), "Gray-Scott style dual-field spot initialization with jitter"},
        {"waves", [] {
            LaunchConfig cfg{16180, GridSpec{192, 192}, ModelTier::Minimal, TemporalPolicy::UniformA};
            cfg.initialConditions.type = InitialConditionType::Waves;
            cfg.initialConditions.waves.targetVariable = "surface_water_w";
            cfg.initialConditions.waves.baseline = 0.0f;
            cfg.initialConditions.waves.dropAmplitude = 1.0f;
            cfg.initialConditions.waves.dropRadius = 14.0f;
            cfg.initialConditions.waves.dropCount = 4;
            cfg.initialConditions.waves.dropJitter = 0.45f;
            cfg.initialConditions.waves.ringFrequency = 1.8f;
            return cfg;
        }(), "Multi-drop wave initialization centered on surface_water_w"},
        {"blank", [] {
            LaunchConfig cfg{1234, GridSpec{128, 128}, ModelTier::Minimal, TemporalPolicy::UniformA};
            cfg.initialConditions.type = InitialConditionType::Blank;
            return cfg;
        }(), "Zero-initialized canonical fields"}
    };
    return presets;
}

std::optional<LaunchPreset> presetByName(const std::string& name) {
    const auto normalized = toLower(name);
    for (const auto& preset : allPresets()) {
        if (toLower(preset.name) == normalized) {
            return preset;
        }
    }
    return std::nullopt;
}

FieldSummary summarizeField(const StateStoreSnapshot::FieldPayload& field) {
    FieldSummary summary;

    bool minMaxInitialized = false;
    double total = 0.0;
    const auto count = std::min(field.values.size(), field.validityMask.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (field.validityMask[i] == 0u) {
            summary.invalidCount += 1;
            continue;
        }

        const float value = field.values[i];
        if (!std::isfinite(value)) {
            summary.invalidCount += 1;
            continue;
        }

        if (!minMaxInitialized) {
            summary.minValue = value;
            summary.maxValue = value;
            minMaxInitialized = true;
        } else {
            summary.minValue = std::min(summary.minValue, value);
            summary.maxValue = std::max(summary.maxValue, value);
        }

        summary.validCount += 1;
        total += static_cast<double>(value);
    }

    if (summary.validCount > 0) {
        summary.average = total / static_cast<double>(summary.validCount);
    }

    return summary;
}

char heatmapGlyph(const float value, const float minValue, const float maxValue) {
    static constexpr std::string_view palette = " .:-=+*#%@";
    if (!std::isfinite(value)) {
        return '?';
    }
    if (maxValue <= minValue) {
        return palette[palette.size() / 2];
    }

    const float normalized = std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
    const std::size_t idx = static_cast<std::size_t>(normalized * static_cast<float>(palette.size() - 1));
    return palette[idx];
}

} // namespace ws::app
