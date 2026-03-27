#include "ws/gui/main_window/detail_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ws::gui::main_window::detail {

std::vector<float> mergedFieldValues(const StateStoreSnapshot::FieldPayload& field, const bool includeSparseOverlay) {
    const std::size_t n = field.values.size();
    std::vector<float> merged(n, std::numeric_limits<float>::quiet_NaN());

    for (std::size_t i = 0; i < n; ++i) {
        if (i < field.validityMask.size() && field.validityMask[i] != 0u) {
            merged[i] = field.values[i];
        }
    }

    if (includeSparseOverlay) {
        for (const auto& [idx, value] : field.sparseOverlay) {
            if (idx < merged.size()) {
                merged[static_cast<std::size_t>(idx)] = value;
            }
        }
    }

    return merged;
}

void minMaxFinite(const std::vector<float>& values, float& outMin, float& outMax) {
    outMin = std::numeric_limits<float>::infinity();
    outMax = -std::numeric_limits<float>::infinity();
    for (const float value : values) {
        if (std::isfinite(value)) {
            outMin = std::min(outMin, value);
            outMax = std::max(outMax, value);
        }
    }
    if (!std::isfinite(outMin) || !std::isfinite(outMax)) {
        outMin = 0.0f;
        outMax = 1.0f;
    }
    if (std::abs(outMax - outMin) < 1e-12f) {
        outMax = outMin + 1.0f;
    }
}

std::uint64_t hashCombine(const std::uint64_t seed, const std::uint64_t value) {
    constexpr std::uint64_t k = 0x9e3779b97f4a7c15ull;
    return seed ^ (value + k + (seed << 6u) + (seed >> 2u));
}

std::uint64_t hashFloat(const float value) {
    return static_cast<std::uint64_t>(std::hash<float>{}(value));
}

void unpackColor(const ImU32 color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) {
    r = static_cast<std::uint8_t>((color) & 0xFFu);
    g = static_cast<std::uint8_t>((color >> 8u) & 0xFFu);
    b = static_cast<std::uint8_t>((color >> 16u) & 0xFFu);
    a = static_cast<std::uint8_t>((color >> 24u) & 0xFFu);
}

namespace {

float hashNoise(const std::uint64_t seed, const int x, const int y) {
    std::uint64_t h = seed;
    h = hashCombine(h, static_cast<std::uint64_t>(x * 73856093));
    h = hashCombine(h, static_cast<std::uint64_t>(y * 19349663));
    h ^= (h >> 33u);
    h *= 0xff51afd7ed558ccdull;
    h ^= (h >> 33u);
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= (h >> 33u);
    const double normalized = static_cast<double>(h & 0xFFFFFFFFull) / static_cast<double>(0xFFFFFFFFull);
    return static_cast<float>(normalized);
}

struct PreviewZoneSample {
    float zoneValue = 0.5f;
    float edgeBlend = 0.0f;
};

struct PreviewIslandSample {
    float landMask = 0.0f;
    float shelfMask = 0.0f;
};

PreviewZoneSample previewZoneSample(const std::uint64_t seed, const float x, const float y, const float zoneScale) {
    const float px = x / std::max(0.001f, zoneScale);
    const float py = y / std::max(0.001f, zoneScale);
    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));

    float bestDist2 = std::numeric_limits<float>::infinity();
    float secondDist2 = std::numeric_limits<float>::infinity();
    float bestValue = 0.5f;

    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int sx = cx + ox;
            const int sy = cy + oy;
            const float jitterX = hashNoise(seed ^ 0x11ull, sx, sy) - 0.5f;
            const float jitterY = hashNoise(seed ^ 0x37ull, sx, sy) - 0.5f;
            const float siteX = static_cast<float>(sx) + 0.5f + 0.8f * jitterX;
            const float siteY = static_cast<float>(sy) + 0.5f + 0.8f * jitterY;
            const float dx = px - siteX;
            const float dy = py - siteY;
            const float dist2 = dx * dx + dy * dy;

            if (dist2 < bestDist2) {
                secondDist2 = bestDist2;
                bestDist2 = dist2;
                bestValue = hashNoise(seed ^ 0x5Bull, sx, sy);
            } else if (dist2 < secondDist2) {
                secondDist2 = dist2;
            }
        }
    }

    const float edge = std::clamp((std::sqrt(secondDist2) - std::sqrt(bestDist2)) * 0.8f, 0.0f, 1.0f);
    return PreviewZoneSample{bestValue, edge};
}

PreviewIslandSample previewIslandMask(
    const std::uint64_t seed,
    const float nx,
    const float ny,
    const float density,
    const float jitter,
    const float falloff) {
    const float cellScale = std::clamp(0.22f - 0.12f * density, 0.07f, 0.24f);
    const float px = nx / std::max(0.001f, cellScale);
    const float py = ny / std::max(0.001f, cellScale);
    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));

    float best = 0.0f;
    float shelf = 0.0f;
    const float spawnThreshold = std::clamp(0.28f + 0.52f * density, 0.15f, 0.92f);

    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int sx = cx + ox;
            const int sy = cy + oy;
            const float spawn = hashNoise(seed ^ 0xA9ull, sx, sy);
            if (spawn > spawnThreshold) {
                continue;
            }

            const float jx = (hashNoise(seed ^ 0xB4ull, sx, sy) - 0.5f) * jitter;
            const float jy = (hashNoise(seed ^ 0xC8ull, sx, sy) - 0.5f) * jitter;
            const float siteX = static_cast<float>(sx) + 0.5f + jx;
            const float siteY = static_cast<float>(sy) + 0.5f + jy;
            const float radius = std::max(0.18f, 0.38f + 1.20f * hashNoise(seed ^ 0xD1ull, sx, sy));

            const float dx = px - siteX;
            const float dy = py - siteY;
            const float dNorm = std::sqrt(dx * dx + dy * dy) / radius;
            const float core = std::clamp(1.0f - dNorm, 0.0f, 1.0f);
            best = std::max(best, std::pow(core, std::max(0.5f, falloff)));
            shelf = std::max(shelf, std::pow(std::clamp(1.0f - dNorm * 0.72f, 0.0f, 1.0f), 1.7f));
        }
    }

    return PreviewIslandSample{best, shelf};
}

} // namespace

float previewTerrainValue(const PanelState& panel, const int x, const int y, const int w, const int h) {
    const float nx = static_cast<float>(x) / std::max(1, w - 1);
    const float ny = static_cast<float>(y) / std::max(1, h - 1);

    float freq = std::max(0.01f, panel.terrainBaseFrequency);
    float amplitude = std::max(0.01f, panel.terrainAmplitude);
    float value = 0.0f;
    float ampAccum = 0.0f;
    const int octaves = std::clamp(panel.terrainOctaves, 1, 8);

    const float warpX = hashNoise(panel.seed ^ 0xABull, x / 5, y / 5) - 0.5f;
    const float warpY = hashNoise(panel.seed ^ 0xCDull, x / 5, y / 5) - 0.5f;
    const float zoneScale = std::max(0.08f, 0.40f / std::max(0.25f, panel.terrainBaseFrequency));
    const float domainX = nx + warpX * panel.terrainWarpStrength;
    const float domainY = ny + warpY * panel.terrainWarpStrength;
    const PreviewZoneSample zone = previewZoneSample(panel.seed ^ 0xEFull, domainX, domainY, zoneScale);
    const PreviewIslandSample islands = previewIslandMask(
        panel.seed ^ 0x913ull,
        domainX,
        domainY,
        std::clamp(panel.islandDensity, 0.05f, 0.95f),
        std::clamp(panel.archipelagoJitter, 0.0f, 1.5f),
        std::clamp(panel.islandFalloff, 0.35f, 4.5f));
    const float zoneBias = zone.zoneValue - 0.5f;

    for (int i = 0; i < octaves; ++i) {
        const float sampleX = (nx + warpX * panel.terrainWarpStrength) * freq;
        const float sampleY = (ny + warpY * panel.terrainWarpStrength) * freq;
        const float wave = 0.5f + 0.5f * std::sin(sampleX * 6.28318f + 0.5f * std::cos(sampleY * 6.28318f));
        const float noise = hashNoise(panel.seed + static_cast<std::uint64_t>(i * 7919), x + (i * 13), y + (i * 17));
        const float ridge = 1.0f - std::abs(2.0f * noise - 1.0f);
        const float mixed = (wave * (1.0f - panel.terrainRidgeMix)) + (ridge * (panel.terrainRidgeMix + 0.20f * std::clamp(zoneBias, 0.0f, 1.0f)));
        value += mixed * amplitude;
        ampAccum += amplitude;
        freq *= std::max(1.0f, panel.terrainLacunarity);
        amplitude *= std::clamp(panel.terrainGain, 0.1f, 1.0f);
    }

    if (ampAccum > 0.0f) {
        value /= ampAccum;
    }

    const float latitude = std::abs((ny - 0.5f) * 2.0f);
    const float islandLift = 0.56f * islands.landMask + 0.12f * islands.shelfMask - 0.20f * (1.0f - islands.landMask);
    const float erosion = (hashNoise(panel.seed ^ 0x6A5ull, x, y) - 0.5f) * panel.erosionStrength * 0.35f;
    value = value * 0.58f + islandLift;
    value -= latitude * panel.polarCooling * 0.25f;
    value += 0.10f * zoneBias;
    value += 0.08f * (zone.edgeBlend - 0.5f);
    value += erosion;
    value += (hashNoise(panel.seed ^ 0xA53ull, x, y) - 0.5f) * panel.biomeNoiseStrength;
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace ws::gui::main_window::detail
