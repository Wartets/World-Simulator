#include "ws/core/initialization_strategy.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ws::initialization {
namespace {

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

float hash01(const std::uint64_t seed, const int x, const int y) {
    std::uint64_t h = DeterministicHash::combine(seed, DeterministicHash::hashPod(x));
    h = DeterministicHash::combine(h, DeterministicHash::hashPod(y));
    h = mix64(h);
    const std::uint32_t top24 = static_cast<std::uint32_t>((h >> 40u) & 0xFFFFFFu);
    return static_cast<float>(top24) / static_cast<float>(0xFFFFFFu);
}

std::string resolveTargetVariable(StateStore& stateStore, const std::string& token, const std::string& modeName) {
    if (token.empty()) {
        throw std::runtime_error("Initialization target token is empty for mode: " + modeName);
    }
    if (stateStore.hasField(token)) {
        return token;
    }
    if (const auto alias = stateStore.resolveFieldAlias(token); alias.has_value() && stateStore.hasField(*alias)) {
        return *alias;
    }

    const auto generatedId = static_cast<std::uint32_t>(1000000u + stateStore.variableNames().size());
    stateStore.allocateScalarField(VariableSpec{generatedId, token});
    return token;
}

void applyConway(StateStore& stateStore, const RuntimeConfig& config) {
    const auto& p = config.initialConditions.conway;
    const std::string targetToken = p.targetVariable.empty() ? "initialization.conway.target" : p.targetVariable;
    const std::string targetVariable = resolveTargetVariable(stateStore, targetToken, "conway");
    StateStore::WriteSession seedWriter(stateStore, "runtime_seed_pipeline", std::vector<std::string>{targetVariable});

    const float aliveProbability = std::clamp(p.aliveProbability, 0.0f, 1.0f);
    const std::uint32_t width = config.grid.width;
    const std::uint32_t height = config.grid.height;
    std::vector<std::uint8_t> aliveMask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u);

    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            const float r = hash01(DeterministicHash::combine(config.seed, 0xC011CA7ULL), static_cast<int>(x), static_cast<int>(y));
            aliveMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(r < aliveProbability ? 1u : 0u);
        }
    }

    const int smoothingPasses = std::clamp(p.smoothingPasses, 0, 8);
    std::vector<std::uint8_t> scratch = aliveMask;
    auto sampleAlive = [&](const std::vector<std::uint8_t>& mask, int sx, int sy) {
        sx = std::clamp(sx, 0, static_cast<int>(width) - 1);
        sy = std::clamp(sy, 0, static_cast<int>(height) - 1);
        return mask[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)];
    };

    for (int pass = 0; pass < smoothingPasses; ++pass) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                int neighbors = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        neighbors += static_cast<int>(sampleAlive(aliveMask, static_cast<int>(x) + ox, static_cast<int>(y) + oy));
                    }
                }

                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                const bool currentlyAlive = aliveMask[idx] != 0u;
                if (neighbors > 4) {
                    scratch[idx] = 1u;
                } else if (neighbors < 4) {
                    scratch[idx] = 0u;
                } else {
                    scratch[idx] = static_cast<std::uint8_t>(currentlyAlive ? 1u : 0u);
                }
            }
        }
        aliveMask.swap(scratch);
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float value = aliveMask[idx] != 0u ? p.aliveValue : p.deadValue;
            seedWriter.setScalar(targetVariable, Cell{x, y}, value);
        }
    }
}

void applyGrayScott(StateStore& stateStore, const RuntimeConfig& config) {
    const auto& p = config.initialConditions.grayScott;
    const std::string targetAToken = p.targetVariableA.empty() ? "initialization.gray_scott.target_a" : p.targetVariableA;
    const std::string targetBToken = p.targetVariableB.empty() ? "initialization.gray_scott.target_b" : p.targetVariableB;
    const std::string targetA = resolveTargetVariable(stateStore, targetAToken, "gray_scott");
    const std::string targetB = resolveTargetVariable(stateStore, targetBToken, "gray_scott");
    StateStore::WriteSession seedWriter(stateStore, "runtime_seed_pipeline", std::vector<std::string>{targetA, targetB});

    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            const Cell c{x, y};
            seedWriter.setScalar(targetA, c, p.backgroundA);
            seedWriter.setScalar(targetB, c, p.backgroundB);
        }
    }

    const int spotCount = std::max(1, p.spotCount);
    const float spotRadius = std::max(0.5f, p.spotRadius);
    const float spotJitter = std::clamp(p.spotJitter, 0.0f, 1.0f);
    for (int i = 0; i < spotCount; ++i) {
        const float cx = hash01(DeterministicHash::combine(config.seed, 0x6A73C071ULL + static_cast<std::uint64_t>(i)), i, 0) * static_cast<float>(config.grid.width);
        const float cy = hash01(DeterministicHash::combine(config.seed, 0x6A73C072ULL + static_cast<std::uint64_t>(i)), i, 1) * static_cast<float>(config.grid.height);
        const float jitterHash = hash01(DeterministicHash::combine(config.seed, 0x6A73C073ULL + static_cast<std::uint64_t>(i)), i, 2);
        const float radiusScale = std::max(0.20f, 1.0f + ((jitterHash - 0.5f) * 2.0f * spotJitter));
        const float rSq = (spotRadius * radiusScale) * (spotRadius * radiusScale);
        for (std::uint32_t y = 0; y < config.grid.height; ++y) {
            for (std::uint32_t x = 0; x < config.grid.width; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                if ((dx * dx + dy * dy) <= rSq) {
                    const Cell c{x, y};
                    seedWriter.setScalar(targetA, c, p.spotValueA);
                    seedWriter.setScalar(targetB, c, p.spotValueB);
                }
            }
        }
    }
}

void applyWaves(StateStore& stateStore, const RuntimeConfig& config) {
    const auto& p = config.initialConditions.waves;
    const std::string targetToken = p.targetVariable.empty() ? "initialization.waves.target" : p.targetVariable;
    const std::string targetVariable = resolveTargetVariable(stateStore, targetToken, "waves");
    StateStore::WriteSession seedWriter(stateStore, "runtime_seed_pipeline", std::vector<std::string>{targetVariable});
    const float centerX = static_cast<float>(config.grid.width) * 0.5f;
    const float centerY = static_cast<float>(config.grid.height) * 0.5f;
    const float radius = std::max(0.1f, p.dropRadius);
    const int dropCount = std::clamp(p.dropCount, 1, 64);
    const float dropJitter = std::clamp(p.dropJitter, 0.0f, 1.0f);
    const float ringFrequency = std::clamp(p.ringFrequency, 0.25f, 6.0f);
    const float maxOffset = std::min(static_cast<float>(config.grid.width), static_cast<float>(config.grid.height)) * 0.30f * dropJitter;

    std::vector<std::pair<float, float>> dropCenters;
    dropCenters.reserve(static_cast<std::size_t>(dropCount));
    dropCenters.emplace_back(centerX, centerY);
    for (int i = 1; i < dropCount; ++i) {
        const float rx = hash01(DeterministicHash::combine(config.seed, 0x717A1D11ULL + static_cast<std::uint64_t>(i)), i, 0);
        const float ry = hash01(DeterministicHash::combine(config.seed, 0x717A1D12ULL + static_cast<std::uint64_t>(i)), i, 1);
        const float ox = (rx - 0.5f) * 2.0f * maxOffset;
        const float oy = (ry - 0.5f) * 2.0f * maxOffset;
        dropCenters.emplace_back(centerX + ox, centerY + oy);
    }

    for (std::uint32_t y = 0; y < config.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config.grid.width; ++x) {
            float value = p.baseline;
            for (const auto& center : dropCenters) {
                const float dx = static_cast<float>(x) - center.first;
                const float dy = static_cast<float>(y) - center.second;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < radius) {
                    const float t = d / radius;
                    value += p.dropAmplitude * (0.5f + 0.5f * std::cos(t * 3.14159265f * ringFrequency));
                }
            }
            seedWriter.setScalar(targetVariable, Cell{x, y}, value);
        }
    }
}

} // namespace

void applyNonTerrainInitialization(StateStore& stateStore, const RuntimeConfig& config) {
    static_cast<void>(stateStore);
    switch (config.initialConditions.type) {
        case InitialConditionType::Conway:
            applyConway(stateStore, config);
            return;
        case InitialConditionType::GrayScott:
            applyGrayScott(stateStore, config);
            return;
        case InitialConditionType::Waves:
            applyWaves(stateStore, config);
            return;
        case InitialConditionType::Voronoi:
        case InitialConditionType::Clustering:
        case InitialConditionType::SparseRandom:
        case InitialConditionType::GradientField:
        case InitialConditionType::Checkerboard:
        case InitialConditionType::RadialPattern:
        case InitialConditionType::MultiScale:
        case InitialConditionType::DiffusionLimit:
        case InitialConditionType::Blank:
            return;
        case InitialConditionType::Terrain:
            throw std::invalid_argument("applyNonTerrainInitialization called with terrain mode");
        default:
            return;
    }
}

} // namespace ws::initialization
