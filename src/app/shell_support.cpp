#include "ws/app/shell_support.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace ws::app {

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

std::optional<ModelTier> parseTier(const std::string& token) {
    const std::string normalized = toLower(token);
    if (normalized == "a") {
        return ModelTier::A;
    }
    if (normalized == "b") {
        return ModelTier::B;
    }
    if (normalized == "c") {
        return ModelTier::C;
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

ProfileResolverInput buildProfileInput(const ModelTier tier) {
    ProfileResolverInput input;
    for (const auto& subsystem : ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = tier;
    }
    input.compatibilityAssumptions = {
        "interactive_shell",
        "runtime_manual_control"
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
    config.profileInput = buildProfileInput(launchConfig.tier);
    config.worldGen = launchConfig.worldGen;
    return config;
}

const std::vector<LaunchPreset>& allPresets() {
    static const std::vector<LaunchPreset> presets = {
        {"baseline", LaunchConfig{42, GridSpec{128, 128}, ModelTier::A, TemporalPolicy::UniformA}, "Balanced default deterministic setup (square high-context grid)"},
        {"phased_b", LaunchConfig{777, GridSpec{160, 160}, ModelTier::B, TemporalPolicy::PhasedB}, "Intermediate coupling with phased policy"},
        {"dense_c", LaunchConfig{2026, GridSpec{192, 192}, ModelTier::C, TemporalPolicy::MultiRateC}, "Advanced coupling stress-oriented profile"}
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
