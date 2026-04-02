#include "ws/core/runtime.hpp"

#include "ws/core/determinism.hpp"
#include "ws/core/initialization_strategy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace ws {

namespace {

std::string tierToString(const ModelTier tier) {
    return toString(tier);
}

float smoothStep(const float t) {
    const float x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

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

float valueNoise2D(const std::uint64_t seed, const float x, const float y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float tx = smoothStep(x - static_cast<float>(x0));
    const float ty = smoothStep(y - static_cast<float>(y0));

    const float n00 = hash01(seed, x0, y0);
    const float n10 = hash01(seed, x1, y0);
    const float n01 = hash01(seed, x0, y1);
    const float n11 = hash01(seed, x1, y1);

    const float nx0 = n00 + (n10 - n00) * tx;
    const float nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * ty;
}

float fbm2D(
    const std::uint64_t seed,
    const float x,
    const float y,
    const int octaves,
    const float lacunarity,
    const float gain) {
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float amplitudeSum = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        const std::uint64_t octaveSeed = DeterministicHash::combine(seed, static_cast<std::uint64_t>(i + 1));
        total += amplitude * valueNoise2D(octaveSeed, x * frequency, y * frequency);
        amplitudeSum += amplitude;
        frequency *= lacunarity;
        amplitude *= gain;
    }

    if (amplitudeSum <= 1e-6f) {
        return 0.5f;
    }
    return std::clamp(total / amplitudeSum, 0.0f, 1.0f);
}

struct ZoneSample {
    float zoneValue = 0.5f;
    float edgeBlend = 0.0f;
};

struct ArchipelagoSample {
    float landMask = 0.0f;
    float shelfMask = 0.0f;
    float regionBias = 0.0f;
};

ZoneSample macroZoneSample(const std::uint64_t seed, const float x, const float y, const float zoneScale) {
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
            const float jitterX = hash01(DeterministicHash::combine(seed, 0x11ULL), sx, sy) - 0.5f;
            const float jitterY = hash01(DeterministicHash::combine(seed, 0x23ULL), sx, sy) - 0.5f;
            const float siteX = static_cast<float>(sx) + 0.5f + 0.9f * jitterX;
            const float siteY = static_cast<float>(sy) + 0.5f + 0.9f * jitterY;
            const float dx = px - siteX;
            const float dy = py - siteY;
            const float dist2 = dx * dx + dy * dy;

            if (dist2 < bestDist2) {
                secondDist2 = bestDist2;
                bestDist2 = dist2;
                bestValue = hash01(DeterministicHash::combine(seed, 0x37ULL), sx, sy);
            } else if (dist2 < secondDist2) {
                secondDist2 = dist2;
            }
        }
    }

    const float edge = std::clamp((std::sqrt(secondDist2) - std::sqrt(bestDist2)) * 0.8f, 0.0f, 1.0f);
    return ZoneSample{bestValue, edge};
}

ArchipelagoSample sampleArchipelago(
    const std::uint64_t seed,
    const float x,
    const float y,
    const float islandDensity,
    const float jitter,
    const float falloff) {
    const float cellScale = std::clamp(0.22f - 0.12f * islandDensity, 0.07f, 0.24f);
    const float px = x / std::max(0.001f, cellScale);
    const float py = y / std::max(0.001f, cellScale);

    const int cx = static_cast<int>(std::floor(px));
    const int cy = static_cast<int>(std::floor(py));

    float best = 0.0f;
    float shelf = 0.0f;
    float biasAccum = 0.0f;
    float biasWeight = 0.0f;

    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int sx = cx + ox;
            const int sy = cy + oy;

            const float spawn = hash01(DeterministicHash::combine(seed, 0x111ULL), sx, sy);
            const float spawnThreshold = std::clamp(0.28f + 0.52f * islandDensity, 0.15f, 0.92f);
            if (spawn > spawnThreshold) {
                continue;
            }

            const float jx = (hash01(DeterministicHash::combine(seed, 0x222ULL), sx, sy) - 0.5f) * jitter;
            const float jy = (hash01(DeterministicHash::combine(seed, 0x333ULL), sx, sy) - 0.5f) * jitter;
            const float centerX = static_cast<float>(sx) + 0.5f + jx;
            const float centerY = static_cast<float>(sy) + 0.5f + jy;

            const float baseRadius = 0.38f + 1.25f * hash01(DeterministicHash::combine(seed, 0x444ULL), sx, sy);
            const float radius = std::max(0.18f, baseRadius * (0.65f + 0.65f * (1.0f - islandDensity)));

            const float dx = px - centerX;
            const float dy = py - centerY;
            const float dNorm = std::sqrt(dx * dx + dy * dy) / radius;

            const float islandCore = std::clamp(1.0f - dNorm, 0.0f, 1.0f);
            const float softIsland = std::pow(islandCore, std::max(0.55f, falloff));
            const float shelfContribution = std::pow(std::clamp(1.0f - dNorm * 0.72f, 0.0f, 1.0f), 1.8f);

            best = std::max(best, softIsland);
            shelf = std::max(shelf, shelfContribution);

            const float w = std::clamp(1.0f - dNorm, 0.0f, 1.0f);
            biasAccum += w * (hash01(DeterministicHash::combine(seed, 0x777ULL), sx, sy) - 0.5f);
            biasWeight += w;
        }
    }

    const float regionalBias = (biasWeight > 1e-5f) ? (biasAccum / biasWeight) : 0.0f;
    return ArchipelagoSample{best, shelf, regionalBias};
}

std::uint64_t hashInitialConditions(const InitialConditionConfig& initialConditions) {
    std::uint64_t fingerprint = DeterministicHash::hashPod(static_cast<std::uint8_t>(initialConditions.type));

    switch (initialConditions.type) {
        case InitialConditionType::Terrain: {
            const auto& p = initialConditions.terrain;
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainBaseFrequency));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainDetailFrequency));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainWarpStrength));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainAmplitude));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainRidgeMix));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainOctaves));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainLacunarity));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.terrainGain));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.seaLevel));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.polarCooling));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.latitudeBanding));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.humidityFromWater));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.biomeNoiseStrength));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.islandDensity));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.islandFalloff));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.coastlineSharpness));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.archipelagoJitter));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.erosionStrength));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.shelfDepth));
            break;
        }
        case InitialConditionType::Conway: {
            const auto& p = initialConditions.conway;
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(p.targetVariable));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.aliveProbability));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.aliveValue));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.deadValue));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.smoothingPasses));
            break;
        }
        case InitialConditionType::GrayScott: {
            const auto& p = initialConditions.grayScott;
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(p.targetVariableA));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(p.targetVariableB));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.backgroundA));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.backgroundB));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.spotValueA));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.spotValueB));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.spotCount));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.spotRadius));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.spotJitter));
            break;
        }
        case InitialConditionType::Waves: {
            const auto& p = initialConditions.waves;
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(p.targetVariable));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.baseline));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.dropAmplitude));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.dropRadius));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.dropCount));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.dropJitter));
            fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(p.ringFrequency));
            break;
        }
        case InitialConditionType::Blank:
        default:
            break;
    }

    return fingerprint;
}

} // namespace

Runtime::Runtime(RuntimeConfig config)
    : config_(std::move(config)),
    stateStore_(config_.grid, config_.boundaryMode, config_.topologyBackend, config_.memoryLayoutPolicy),
    runtimeGuardrailPolicy_(config_.guardrailPolicy),
    snapshot_{RunSignature(
                0,
                "placeholder",
                GridSpec{1, 1},
                BoundaryMode::Clamp,
                UnitRegime::Normalized,
                TemporalPolicy::UniformA,
                "none",
                "none",
                0,
                0,
                0),
                0,
                StateHeader{},
                0} {
    config_.grid.validate();
    scheduler_.setExecutionPolicyMode(config_.executionPolicyMode);
}

void Runtime::registerSubsystem(std::shared_ptr<ISubsystem> subsystem) {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Subsystem registration is only allowed before runtime start");
    }
    scheduler_.registerSubsystem(std::move(subsystem));
}

void Runtime::selectProfile(ProfileResolverInput profileInput) {
    if (status_ != RuntimeStatus::Created && status_ != RuntimeStatus::Terminated) {
        throw std::runtime_error("Profile selection is only allowed in Created or Terminated state");
    }
    config_.profileInput = std::move(profileInput);
    trace(
        TraceChannel::Configuration,
        "runtime.config.profile_selected",
        "profile resolver input updated",
        DeterministicHash::hashPod(config_.profileInput.requestedSubsystemTiers.size()));
}

void Runtime::updateGuardrailPolicy(NumericGuardrailPolicy guardrailPolicy) {
    runtimeGuardrailPolicy_ = std::move(guardrailPolicy);
    trace(
        TraceChannel::Configuration,
        "runtime.config.guardrails_updated",
        "numeric guardrail policy updated",
        DeterministicHash::hashPod(runtimeGuardrailPolicy_.maxAbsDeltaPerStep));
}

void Runtime::start() {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Runtime start can only be called from Created state");
    }

    try {
        resolvedProfile_ = profileResolver_.resolve(config_.profileInput);
        paused_ = false;

        admissionReport_ = interactionCoordinator_.buildAdmissionReport(
            resolvedProfile_,
            config_.temporalPolicy,
            scheduler_.registeredSubsystems());
        if (!admissionReport_.admitted) {
            throw std::runtime_error("Profile admission failed: " + admissionReport_.diagnosticsText());
        }
        scheduler_.setAdmissionReport(admissionReport_);

        allocateRuntimeFieldsFromModelSpec();

        stateStore_.clearFieldAliases();
        if (config_.modelExecutionSpec.has_value()) {
            for (const auto& [semanticKey, variableId] : config_.modelExecutionSpec->semanticFieldAliases) {
                if (!semanticKey.empty() && !variableId.empty() && stateStore_.hasField(variableId)) {
                    stateStore_.registerFieldAlias(semanticKey, variableId);
                }
            }
        }

        initializeParameterControls();

        const std::vector<std::string> runtimeFields = stateStore_.variableNames();

        {
            StateStore::WriteSession zeroWriter(stateStore_, "runtime_seed_pipeline", runtimeFields);
            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    const Cell c{x, y};
                    for (const auto& field : runtimeFields) {
                        zeroWriter.setScalar(field, c, 0.0f);
                    }
                }
            }
        }

        switch (config_.initialConditions.type) {
            case InitialConditionType::Terrain: {
                StateStore::WriteSession seedWriter(stateStore_, "runtime_seed_pipeline", runtimeFields);

                const float invW = 1.0f / static_cast<float>(std::max<std::uint32_t>(1, config_.grid.width - 1));
                const float invH = 1.0f / static_cast<float>(std::max<std::uint32_t>(1, config_.grid.height - 1));

                const float baseFreq = std::max(0.15f, config_.initialConditions.terrain.terrainBaseFrequency);
                const float detailFreq = std::max(0.25f, config_.initialConditions.terrain.terrainDetailFrequency);
                const float warpStrength = std::clamp(config_.initialConditions.terrain.terrainWarpStrength, 0.0f, 2.0f);
                const float terrainAmplitude = std::clamp(config_.initialConditions.terrain.terrainAmplitude, 0.1f, 3.0f);
                const float ridgeMix = std::clamp(config_.initialConditions.terrain.terrainRidgeMix, 0.0f, 1.0f);
                const int octaves = std::clamp(config_.initialConditions.terrain.terrainOctaves, 1, 8);
                const float lacunarity = std::max(1.0f, config_.initialConditions.terrain.terrainLacunarity);
                const float gain = std::clamp(config_.initialConditions.terrain.terrainGain, 0.0f, 1.0f);
                const float seaLevel = std::clamp(config_.initialConditions.terrain.seaLevel, 0.0f, 1.0f);
                const float polarCooling = std::clamp(config_.initialConditions.terrain.polarCooling, 0.0f, 1.5f);
                const float banding = std::clamp(config_.initialConditions.terrain.latitudeBanding, 0.0f, 2.0f);
                const float humidityFromWater = std::clamp(config_.initialConditions.terrain.humidityFromWater, 0.0f, 1.5f);
                const float biomeNoiseStrength = std::clamp(config_.initialConditions.terrain.biomeNoiseStrength, 0.0f, 1.0f);
                const float islandDensity = std::clamp(config_.initialConditions.terrain.islandDensity, 0.05f, 0.95f);
                const float islandFalloff = std::clamp(config_.initialConditions.terrain.islandFalloff, 0.35f, 4.5f);
                const float coastlineSharpness = std::clamp(config_.initialConditions.terrain.coastlineSharpness, 0.25f, 4.0f);
                const float archipelagoJitter = std::clamp(config_.initialConditions.terrain.archipelagoJitter, 0.0f, 1.5f);
                const float erosionStrength = std::clamp(config_.initialConditions.terrain.erosionStrength, 0.0f, 1.0f);
                const float shelfDepth = std::clamp(config_.initialConditions.terrain.shelfDepth, 0.0f, 0.8f);

                const std::uint64_t noiseSeedA = DeterministicHash::combine(config_.seed, 0xA1C31A5DULL);
                const std::uint64_t noiseSeedB = DeterministicHash::combine(config_.seed, 0xB73F42D1ULL);
                const std::uint64_t noiseSeedC = DeterministicHash::combine(config_.seed, 0xCA1F0E91ULL);
                const std::uint64_t noiseSeedD = DeterministicHash::combine(config_.seed, 0xD4E8B8D3ULL);
                const std::uint64_t noiseSeedE = DeterministicHash::combine(config_.seed, 0xE713944BULL);
                const std::uint64_t zoneSeed = DeterministicHash::combine(config_.seed, 0x9B1D44AFULL);
                const std::uint64_t zoneSeed2 = DeterministicHash::combine(config_.seed, 0x64D7A21BULL);
                const std::uint64_t islandSeed = DeterministicHash::combine(config_.seed, 0x14ACD95BULL);

                const float aspect = static_cast<float>(config_.grid.width) / static_cast<float>(std::max<std::uint32_t>(1, config_.grid.height));
                const float zoneScale = std::max(0.08f, 0.40f / std::max(0.25f, baseFreq));

                const auto resolveSeedTarget = [&](const std::string& semanticKey) -> std::optional<std::string> {
                    if (semanticKey.empty()) {
                        return std::nullopt;
                    }
                    if (const auto alias = stateStore_.resolveFieldAlias(semanticKey); alias.has_value() && stateStore_.hasField(*alias)) {
                        return *alias;
                    }
                    if (stateStore_.hasField(semanticKey)) {
                        return semanticKey;
                    }
                    return std::nullopt;
                };

                const auto terrainElevationVar = resolveSeedTarget("generation.elevation");
                const auto hydrologyWaterVar = resolveSeedTarget("hydrology.water");
                const auto temperatureVar = resolveSeedTarget("temperature.current");
                const auto humidityVar = resolveSeedTarget("humidity.current");
                const auto windAxisXVar = resolveSeedTarget("wind.vector.axis_x");
                const auto windAxisYVar = resolveSeedTarget("wind.vector.axis_y");
                const auto climateVar = resolveSeedTarget("climate.current");
                const auto soilFertilityVar = resolveSeedTarget("soil.fertility");
                const auto vegetationVar = resolveSeedTarget("vegetation.current");
                const auto resourcesVar = resolveSeedTarget("resources.current");
                const auto eventSignalVar = resolveSeedTarget("events.signal");
                const auto eventWaterDeltaVar = resolveSeedTarget("events.water_delta");
                const auto eventTemperatureDeltaVar = resolveSeedTarget("events.temperature_delta");
                const std::optional<std::string> seedProbeVar = stateStore_.hasField("seed_probe")
                    ? std::optional<std::string>("seed_probe")
                    : std::nullopt;

                for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                    for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                        const float nx = static_cast<float>(x) * invW;
                        const float ny = static_cast<float>(y) * invH;

                        const float noiseX = nx * aspect;
                        const float noiseY = ny;

                        const float warpX = fbm2D(noiseSeedA, noiseX * detailFreq, noiseY * detailFreq, 3, lacunarity, gain);
                        const float warpY = fbm2D(noiseSeedB, noiseX * detailFreq, noiseY * detailFreq, 3, lacunarity, gain);
                        const float domainX = noiseX + (warpX - 0.5f) * warpStrength;
                        const float domainY = noiseY + (warpY - 0.5f) * warpStrength;

                        const ZoneSample zone = macroZoneSample(zoneSeed, domainX, domainY, zoneScale);
                        const ZoneSample zone2 = macroZoneSample(zoneSeed2, domainX + 0.37f, domainY - 0.29f, zoneScale * 0.65f);
                        const ArchipelagoSample islands = sampleArchipelago(
                            islandSeed,
                            domainX,
                            domainY,
                            islandDensity,
                            archipelagoJitter,
                            islandFalloff);
                        const float zoneBias = (zone.zoneValue - 0.5f);
                        const float zoneBias2 = (zone2.zoneValue - 0.5f);

                        const float continental = fbm2D(noiseSeedC, domainX * baseFreq, domainY * baseFreq, octaves, lacunarity, gain);
                        const float detail = fbm2D(noiseSeedD, domainX * detailFreq, domainY * detailFreq, octaves - 1, lacunarity, gain);
                        const float ridge = 1.0f - std::abs(2.0f * detail - 1.0f);
                        const float biomeNoise = fbm2D(noiseSeedE, noiseX * (detailFreq * 0.5f), noiseY * (detailFreq * 0.5f), 3, 2.0f, 0.6f);

                        const float humidityBiomeAxis = std::clamp(0.5f + 1.4f * zoneBias2 + 0.4f * (biomeNoise - 0.5f), 0.0f, 1.0f);
                        const float mountainBiomeAxis = std::clamp(0.5f + 1.6f * zoneBias + 0.25f * (zone2.edgeBlend - 0.5f), 0.0f, 1.0f);

                        const float tundraW = std::clamp((1.0f - humidityBiomeAxis) * (1.0f - mountainBiomeAxis), 0.0f, 1.0f);
                        const float plainsW = std::clamp(humidityBiomeAxis * (1.0f - mountainBiomeAxis), 0.0f, 1.0f);
                        const float highlandW = std::clamp((1.0f - humidityBiomeAxis) * mountainBiomeAxis, 0.0f, 1.0f);
                        const float wetlandW = std::clamp(humidityBiomeAxis * mountainBiomeAxis, 0.0f, 1.0f);
                        const float biomeWeightSum = std::max(1e-5f, tundraW + plainsW + highlandW + wetlandW);

                        const float biomeTempBias = (
                            tundraW * (-0.14f) +
                            plainsW * (0.02f) +
                            highlandW * (-0.07f) +
                            wetlandW * (0.05f)) / biomeWeightSum;
                        const float biomeHumidityBias = (
                            tundraW * (-0.12f) +
                            plainsW * (0.00f) +
                            highlandW * (-0.04f) +
                            wetlandW * (0.16f)) / biomeWeightSum;
                        const float biomeReliefBias = (
                            tundraW * (0.02f) +
                            plainsW * (-0.04f) +
                            highlandW * (0.16f) +
                            wetlandW * (-0.06f)) / biomeWeightSum;

                        const float regionMountains = std::clamp(0.5f + 1.8f * zoneBias, 0.0f, 1.0f);
                        const float regionHumidity = std::clamp(0.5f - 1.4f * zoneBias, 0.0f, 1.0f);
                        const float macroShape = std::clamp(0.45f + 0.55f * continental + 0.20f * (zone.edgeBlend - 0.5f), 0.0f, 1.0f);
                        const float baseRelief = std::clamp(
                            (continental * (0.50f + 0.18f * regionMountains)) +
                            (ridge * (0.16f + 0.22f * ridgeMix + biomeReliefBias)) +
                            (macroShape * 0.18f),
                            0.0f,
                            1.0f);

                        const float islandInfluence = std::clamp(0.40f + 0.90f * islands.landMask + 0.30f * islands.regionBias, 0.0f, 1.0f);
                        const float erosion = std::clamp((biomeNoise - ridge) * erosionStrength, -0.35f, 0.35f);
                        const float elevationRaw = std::clamp(
                            baseRelief * 0.45f +
                            islandInfluence * 0.55f +
                            0.12f * islands.shelfMask +
                            erosion,
                            0.0f,
                            1.0f);
                        const float elevation = std::clamp(0.5f + (elevationRaw - 0.5f) * terrainAmplitude, 0.0f, 1.0f);

                        const float seaLevelLocal = std::clamp(
                            seaLevel - 0.12f * islands.shelfMask + shelfDepth * (0.20f - islands.shelfMask * 0.25f),
                            0.0f,
                            1.0f);
                        const float coastDelta = (seaLevelLocal - elevation) * coastlineSharpness;
                        const float waterBasin = std::clamp(0.5f + coastDelta * 2.0f + 0.18f * (1.0f - islands.landMask), 0.0f, 1.0f);
                        const float coastalNoise = (biomeNoise - 0.5f) * 0.25f;
                        const float water = std::clamp(waterBasin + coastalNoise, 0.0f, 1.0f);

                        const float latitudinal = 1.0f - std::abs(2.0f * ny - 1.0f);
                        const float heightCooling = std::clamp(elevation * 0.45f, 0.0f, 1.0f);
                        const float zoneTemperatureBias = std::clamp(0.5f + zoneBias * 0.9f, 0.0f, 1.0f);
                        const float temperature = std::clamp(
                            0.25f +
                                0.55f * latitudinal * banding * (1.0f - 0.35f * polarCooling) +
                                0.20f * (1.0f - heightCooling) +
                                0.15f * (zoneTemperatureBias - 0.5f) +
                                biomeTempBias +
                                (biomeNoise - 0.5f) * biomeNoiseStrength,
                            0.0f,
                            1.0f);

                        const float humidity = std::clamp(
                            0.20f +
                                humidityFromWater * 0.45f * water +
                                0.20f * regionHumidity +
                                biomeHumidityBias +
                                0.25f * (1.0f - std::abs(temperature - 0.55f)) +
                                (biomeNoise - 0.5f) * 0.2f,
                            0.0f,
                            1.0f);

                        const float wind = std::clamp(
                            0.25f +
                                0.45f * fbm2D(noiseSeedB, noiseX * 4.0f + 11.0f, noiseY * 4.0f + 7.0f, 3, 2.0f, 0.6f) +
                                0.30f * std::abs(temperature - 0.5f),
                            0.0f,
                            1.0f);
                        const float windV = std::clamp(
                            (fbm2D(noiseSeedD, noiseX * 4.0f + 19.0f, noiseY * 4.0f + 13.0f, 3, 2.0f, 0.6f) - 0.5f) * 2.0f +
                                0.25f * (humidity - 0.5f),
                            -1.0f,
                            1.0f);

                        const float climate = std::clamp(0.45f * temperature + 0.55f * humidity, 0.0f, 1.0f);
                        const float fertility = std::clamp(0.25f + 0.55f * humidity + 0.20f * (1.0f - std::abs(elevation - seaLevel)), 0.0f, 1.0f);
                        const float vegetation = std::clamp(0.25f + 0.55f * fertility * humidity, 0.0f, 1.0f);
                        const float resources = std::clamp(0.15f + 0.55f * vegetation + 0.15f * water + 0.15f * biomeNoise, 0.0f, 1.0f);
                        const float eventSignal = std::clamp(0.15f + 0.85f * fbm2D(noiseSeedA, domainX * 10.0f + 3.0f, domainY * 10.0f + 5.0f, 3, 2.1f, 0.58f), 0.0f, 1.0f);

                        const auto writeIfResolved = [&](const std::optional<std::string>& variableName, const float value) {
                            if (variableName.has_value()) {
                                seedWriter.setScalar(*variableName, Cell{x, y}, value);
                            }
                        };

                        writeIfResolved(terrainElevationVar, elevation);
                        writeIfResolved(hydrologyWaterVar, water);
                        writeIfResolved(temperatureVar, temperature);
                        writeIfResolved(humidityVar, humidity);
                        writeIfResolved(windAxisXVar, wind);
                        writeIfResolved(windAxisYVar, windV);
                        writeIfResolved(climateVar, climate);
                        writeIfResolved(soilFertilityVar, fertility);
                        writeIfResolved(vegetationVar, vegetation);
                        writeIfResolved(resourcesVar, resources);
                        writeIfResolved(eventSignalVar, eventSignal);
                        writeIfResolved(eventWaterDeltaVar, 0.0f);
                        writeIfResolved(eventTemperatureDeltaVar, 0.0f);
                        writeIfResolved(seedProbeVar, continental);
                    }
                }
                break;
            }
            case InitialConditionType::Conway: {
                initialization::applyNonTerrainInitialization(stateStore_, config_);
                break;
            }
            case InitialConditionType::GrayScott: {
                initialization::applyNonTerrainInitialization(stateStore_, config_);
                break;
            }
            case InitialConditionType::Waves: {
                initialization::applyNonTerrainInitialization(stateStore_, config_);
                break;
            }
            case InitialConditionType::Blank:
            default:
                initialization::applyNonTerrainInitialization(stateStore_, config_);
                break;
        }

        StateHeader header{};
        header.stepIndex = 0;
        header.timestampTicks = 0;
        header.status = RuntimeStatus::Initialized;
        stateStore_.setHeader(header);

        scheduler_.initialize(stateStore_, resolvedProfile_);

        const auto activeSubsystems = scheduler_.activeSubsystemNames();
        const std::string subsystemHash = stableHashForStringSet(activeSubsystems);

        std::string profileDigestData;
        for (const auto& [subsystem, tier] : resolvedProfile_.subsystemTiers) {
            profileDigestData += subsystem + ":" + tierToString(tier) + ";";
        }

        const std::uint64_t profileHash = DeterministicHash::hashString(profileDigestData);
        const std::uint64_t initConfigHash = hashInitialConditions(config_.initialConditions);
        const std::string initializationHash = std::to_string(DeterministicHash::combine(profileHash, initConfigHash));
        const std::string eventTimelineHash = std::to_string(DeterministicHash::hashString(
            "baseline:no-events;interaction_graph=" + std::to_string(DeterministicHash::hashString(admissionReport_.serializedGraph)) +
            ";admission_fingerprint=" + std::to_string(admissionReport_.fingerprint)));

        snapshot_.runSignature = runSignatureService_.create(RunIdentityInput{
            .globalSeed = config_.seed,
            .initializationParameterHash = initializationHash,
            .grid = config_.grid,
            .boundaryMode = config_.boundaryMode,
            .unitRegime = config_.unitRegime,
            .temporalPolicy = config_.temporalPolicy,
            .eventTimelineHash = eventTimelineHash,
            .activeSubsystemSetHash = subsystemHash,
            .profile = resolvedProfile_});

        trace(
            TraceChannel::Configuration,
            "runtime.start",
            "runtime admitted and started; execution_policy=" + toString(config_.executionPolicyMode),
            admissionReport_.fingerprint,
            0);

        header.status = RuntimeStatus::Running;
        stateStore_.setHeader(header);
        snapshot_.stateHeader = stateStore_.header();
        snapshot_.stateHash = computeStateHash();
        snapshot_.reproducibilityClass = admissionReport_.reproducibilityClass;
        snapshot_.payloadBytes = stateStore_.createSnapshot(
            snapshot_.runSignature.identityHash(),
            resolvedProfile_.fingerprint(),
            "runtime_start_baseline")
                                     .payloadBytes;
        probeManager_.clearSamples();
        probeManager_.recordAll(stateStore_, header.stepIndex, static_cast<float>(header.timestampTicks));
        stateHashHistory_.clear();
        stateHashHistory_.push_back(snapshot_.stateHash);
        status_ = RuntimeStatus::Running;
    } catch (...) {
        status_ = RuntimeStatus::Error;
        StateHeader header = stateStore_.header();
        header.status = RuntimeStatus::Error;
        stateStore_.setHeader(header);
        throw;
    }
}

void Runtime::initializeParameterControls() {
    parameterControls_.clear();

    for (const auto& control : config_.modelParameterControls) {
        if (control.name.empty() || control.targetVariable.empty()) {
            continue;
        }
        if (!stateStore_.hasField(control.targetVariable)) {
            continue;
        }

        ParameterControl normalized = control;
        if (normalized.minValue > normalized.maxValue) {
            std::swap(normalized.minValue, normalized.maxValue);
        }
        normalized.defaultValue = std::clamp(normalized.defaultValue, normalized.minValue, normalized.maxValue);
        normalized.value = std::clamp(normalized.value, normalized.minValue, normalized.maxValue);
        parameterControls_.insert_or_assign(normalized.name, std::move(normalized));
    }

    if (parameterControls_.empty()) {
        const auto resolveControlTarget = [&](const std::string& semanticKey) -> std::optional<std::string> {
            if (const auto alias = stateStore_.resolveFieldAlias(semanticKey); alias.has_value() && stateStore_.hasField(*alias)) {
                return *alias;
            }
            if (stateStore_.hasField(semanticKey)) {
                return semanticKey;
            }
            return std::nullopt;
        };

        if (const auto waterDeltaTarget = resolveControlTarget("events.water_delta"); waterDeltaTarget.has_value()) {
            parameterControls_.emplace(
                "forcing.water_delta",
                ParameterControl{"forcing.water_delta", *waterDeltaTarget, 0.0f, -1.0f, 1.0f, 0.0f, "1"});
        }
        if (const auto temperatureDeltaTarget = resolveControlTarget("events.temperature_delta"); temperatureDeltaTarget.has_value()) {
            parameterControls_.emplace(
                "forcing.temperature_delta",
                ParameterControl{"forcing.temperature_delta", *temperatureDeltaTarget, 0.0f, -1.0f, 1.0f, 0.0f, "1"});
        }
    }
}

void Runtime::step() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime step requires Running state");
    }
    if (paused_) {
        throw std::runtime_error("Runtime is paused; use control surface stepping");
    }

    stepImpl(false);
}

void Runtime::pause() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime pause requires Running state");
    }
    paused_ = true;
    trace(TraceChannel::Control, "control.pause", "runtime paused");
}

void Runtime::resume() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime resume requires Running state");
    }
    paused_ = false;
    trace(TraceChannel::Control, "control.resume", "runtime resumed");
}

void Runtime::controlledStep(const std::uint32_t stepCount) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Controlled stepping requires Running state");
    }
    if (stepCount == 0) {
        return;
    }

    trace(
        TraceChannel::Control,
        "control.step",
        "runtime controlled stepping",
        DeterministicHash::hashPod(stepCount));
    for (std::uint32_t i = 0; i < stepCount; ++i) {
        stepImpl(true);
    }
}

void Runtime::stepImpl(const bool controlledByRuntimeControl) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime step requires Running state");
    }

    StepDiagnostics diagnostics;
    diagnostics.orderingLog.push_back("pipeline:input_ingest");

    while (eventQueue_.hasInput()) {
        diagnostics.inputPatchesApplied += applyInputFrame(eventQueue_.popInput());
    }

    const std::uint64_t currentStep = stateStore_.header().stepIndex;
    std::vector<PerturbationSpec> stillPending;
    stillPending.reserve(pendingPerturbations_.size());
    for (const auto& perturbation : pendingPerturbations_) {
        const std::uint64_t start = perturbation.startStep;
        const std::uint64_t end = start + std::max<std::uint32_t>(1u, perturbation.durationSteps);
        if (currentStep >= start && currentStep < end) {
            eventQueue_.enqueueEvent(buildPerturbationEvent(perturbation, currentStep));
        }
        if (currentStep + 1 < end) {
            stillPending.push_back(perturbation);
        }
    }
    pendingPerturbations_.swap(stillPending);

    diagnostics.orderingLog.push_back("pipeline:event_queue_apply");
    std::uint64_t eventOrdinal = 0;
    while (eventQueue_.hasEvent()) {
        RuntimeEvent event = eventQueue_.popEvent();
        diagnostics.eventPatchesApplied += applyEvent(event, eventOrdinal);
        diagnostics.eventsApplied += 1;
        eventChronology_.push_back(RuntimeEventRecord{
            stateStore_.header().stepIndex,
            eventOrdinal,
            std::move(event)});
        eventOrdinal += 1;
    }

    StepDiagnostics schedulerDiagnostics = scheduler_.step(
        stateStore_,
        resolvedProfile_,
        config_.temporalPolicy,
        runtimeGuardrailPolicy_,
        stateStore_.header().stepIndex);
    scheduler_.validateObservedDataFlow();

    diagnostics.orderingLog.insert(
        diagnostics.orderingLog.end(),
        schedulerDiagnostics.orderingLog.begin(),
        schedulerDiagnostics.orderingLog.end());
    diagnostics.stabilityAlerts = std::move(schedulerDiagnostics.stabilityAlerts);
    diagnostics.constraintViolations = std::move(schedulerDiagnostics.constraintViolations);
    diagnostics.reproducibilityClass = schedulerDiagnostics.reproducibilityClass;
    diagnostics.stability = schedulerDiagnostics.stability;
    lastStepDiagnostics_ = std::move(diagnostics);

    StateHeader header = stateStore_.header();
    header.stepIndex += 1;
    header.timestampTicks += 1;
    stateStore_.setHeader(header);

    snapshot_.stateHeader = header;
    snapshot_.stateHash = computeStateHash();
    snapshot_.reproducibilityClass = lastStepDiagnostics_.reproducibilityClass;
    snapshot_.stabilityDiagnostics = lastStepDiagnostics_.stability;
    snapshot_.payloadBytes = stateStore_.createSnapshot(
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        "runtime_step")
                                 .payloadBytes;
    probeManager_.recordAll(stateStore_, header.stepIndex, static_cast<float>(header.timestampTicks));
    stateHashHistory_.push_back(snapshot_.stateHash);

    trace(
        TraceChannel::Scheduler,
        "runtime.step.commit",
        controlledByRuntimeControl ? "controlled_step" : "free_step",
        snapshot_.stateHash,
        header.stepIndex);
}

void Runtime::queueInput(RuntimeInputFrame inputFrame) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime input queue requires Running state");
    }
    for (const auto& patch : inputFrame.scalarPatches) {
        trace(
            TraceChannel::Input,
            "runtime.input.patch.queued",
            "input patch queued for variable=" + patch.variableName,
            DeterministicHash::hashString(patch.variableName));
    }
    eventQueue_.enqueueInput(std::move(inputFrame));
}

void Runtime::enqueueEvent(RuntimeEvent event) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime event queue requires Running state");
    }
    trace(
        TraceChannel::Event,
        "runtime.event.queued",
        "runtime event queued name=" + event.eventName,
        DeterministicHash::hashString(event.eventName));
    eventQueue_.enqueueEvent(std::move(event));
}

void Runtime::stop() {
    if (status_ == RuntimeStatus::Terminated) {
        return;
    }

    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Error && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime stop called from invalid state");
    }

    StateHeader header = stateStore_.header();
    header.status = RuntimeStatus::Terminated;
    stateStore_.setHeader(header);

    snapshot_.stateHeader = header;
    snapshot_.stateHash = computeStateHash();
    snapshot_.reproducibilityClass = lastStepDiagnostics_.reproducibilityClass;
    snapshot_.stabilityDiagnostics = lastStepDiagnostics_.stability;
    snapshot_.payloadBytes = stateStore_.createSnapshot(
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        "runtime_stop")
                                 .payloadBytes;
    stateHashHistory_.push_back(snapshot_.stateHash);
    trace(TraceChannel::Control, "runtime.stop", "runtime terminated", snapshot_.stateHash);
    status_ = RuntimeStatus::Terminated;
}

RuntimeCheckpoint Runtime::createCheckpoint(const std::string& label, const bool computeHash) const {
    if (status_ == RuntimeStatus::Created || status_ == RuntimeStatus::Error) {
        throw std::runtime_error("Runtime checkpoint creation requires Initialized, Running, or Terminated state");
    }

    const std::uint64_t profileFingerprint = resolvedProfile_.fingerprint();
    RuntimeCheckpoint checkpoint{
        snapshot_.runSignature,
        profileFingerprint,
        stateStore_.createSnapshot(
            snapshot_.runSignature.identityHash(),
            profileFingerprint,
            label,
            computeHash)};
    checkpoint.manualEventLog = eventQueue_.manualEvents();
    return checkpoint;
}

void Runtime::loadCheckpoint(const RuntimeCheckpoint& checkpoint) {
    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime checkpoint load requires Initialized or Running state");
    }

    if (checkpoint.runSignature.identityHash() != snapshot_.runSignature.identityHash()) {
        throw std::invalid_argument("Checkpoint run signature identity hash does not match active runtime");
    }

    if (checkpoint.profileFingerprint != resolvedProfile_.fingerprint()) {
        throw std::invalid_argument("Checkpoint profile fingerprint does not match active runtime profile");
    }

    stateStore_.loadSnapshot(
        checkpoint.stateSnapshot,
        checkpoint.runSignature.identityHash(),
        checkpoint.profileFingerprint);

    snapshot_.stateHeader = stateStore_.header();
    snapshot_.stateHash = computeStateHash();
    snapshot_.reproducibilityClass = admissionReport_.reproducibilityClass;
    snapshot_.payloadBytes = checkpoint.stateSnapshot.payloadBytes;
    stateHashHistory_.clear();
    stateHashHistory_.push_back(snapshot_.stateHash);
    eventQueue_.setManualEvents(checkpoint.manualEventLog);
    probeManager_.clearSamples();
    probeManager_.recordAll(
        stateStore_,
        snapshot_.stateHeader.stepIndex,
        static_cast<float>(snapshot_.stateHeader.timestampTicks));

    trace(
        TraceChannel::Replay,
        "runtime.checkpoint.loaded",
        "checkpoint loaded label=" + checkpoint.stateSnapshot.checkpointLabel,
        checkpoint.stateSnapshot.stateHash,
        checkpoint.stateSnapshot.header.stepIndex);
}

void Runtime::resetToCheckpoint(const RuntimeCheckpoint& checkpoint) {
    trace(
        TraceChannel::Control,
        "control.reset",
        "runtime reset requested",
        checkpoint.stateSnapshot.stateHash,
        checkpoint.stateSnapshot.header.stepIndex);
    loadCheckpoint(checkpoint);
    eventQueue_.clearTransient();
    pendingPerturbations_.clear();
}

std::vector<ParameterControl> Runtime::parameterControls() const {
    std::vector<ParameterControl> controls;
    controls.reserve(parameterControls_.size());
    for (const auto& [_, control] : parameterControls_) {
        controls.push_back(control);
    }
    std::sort(controls.begin(), controls.end(), [](const ParameterControl& a, const ParameterControl& b) {
        return a.name < b.name;
    });
    return controls;
}

bool Runtime::sampleCurrentValue(
    const std::string& variableName,
    const std::optional<Cell> cell,
    float& outValue,
    std::string& message) const {
    if (!stateStore_.hasField(variableName)) {
        message = "manual_patch_failed reason=unknown_variable variable=" + variableName;
        return false;
    }

    const Cell sampleCell = cell.value_or(Cell{0u, 0u});
    const auto sample = stateStore_.trySampleScalar(
        variableName,
        CellSigned{static_cast<std::int64_t>(sampleCell.x), static_cast<std::int64_t>(sampleCell.y)});
    outValue = sample.value_or(0.0f);
    return true;
}

bool Runtime::setParameterValue(const std::string& parameterName, const float value, std::string note, std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "parameter_set_failed reason=runtime_not_running";
        return false;
    }

    auto it = parameterControls_.find(parameterName);
    if (it == parameterControls_.end()) {
        message = "parameter_set_failed reason=unknown_parameter name=" + parameterName;
        return false;
    }

    ParameterControl& control = it->second;
    if (!stateStore_.hasField(control.targetVariable)) {
        message = "parameter_set_failed reason=unknown_target_variable variable=" + control.targetVariable;
        return false;
    }
    const float clampedValue = std::clamp(value, control.minValue, control.maxValue);

    RuntimeInputFrame frame;
    frame.scalarPatches.reserve(static_cast<std::size_t>(config_.grid.cellCount()));
    for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
        for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
            frame.scalarPatches.push_back(ScalarWritePatch{control.targetVariable, Cell{x, y}, clampedValue});
        }
    }
    queueInput(std::move(frame));

    ManualEventRecord record;
    record.step = stateStore_.header().stepIndex;
    record.time = static_cast<float>(stateStore_.header().timestampTicks);
    record.variable = control.targetVariable;
    record.cellIndex = std::numeric_limits<std::uint64_t>::max();
    record.oldValue = control.value;
    record.newValue = clampedValue;
    record.description = note.empty() ? ("parameter=" + parameterName) : std::move(note);
    record.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    record.kind = ManualEventKind::ParameterUpdate;
    eventQueue_.recordManualEvent(record);

    control.value = clampedValue;
    message = "parameter_set name=" + parameterName + " value=" + std::to_string(clampedValue);
    return true;
}

bool Runtime::applyManualPatch(
    const std::string& variableName,
    const std::optional<Cell> cell,
    const float newValue,
    std::string note,
    std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "manual_patch_failed reason=runtime_not_running";
        return false;
    }
    if (!paused_) {
        message = "manual_patch_failed reason=runtime_not_paused";
        return false;
    }
    if (!std::isfinite(newValue)) {
        message = "manual_patch_failed reason=non_finite_value";
        return false;
    }

    float oldValue = 0.0f;
    if (!sampleCurrentValue(variableName, cell, oldValue, message)) {
        return false;
    }

    RuntimeInputFrame inputFrame;
    if (cell.has_value()) {
        inputFrame.scalarPatches.push_back(ScalarWritePatch{variableName, *cell, newValue});
    } else {
        inputFrame.scalarPatches.reserve(static_cast<std::size_t>(config_.grid.cellCount()));
        for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
            for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                inputFrame.scalarPatches.push_back(ScalarWritePatch{variableName, Cell{x, y}, newValue});
            }
        }
    }
    queueInput(std::move(inputFrame));

    ManualEventRecord record;
    record.step = stateStore_.header().stepIndex;
    record.time = static_cast<float>(stateStore_.header().timestampTicks);
    record.variable = variableName;
    record.cellIndex = cell.has_value() ? stateStore_.indexOf(*cell) : std::numeric_limits<std::uint64_t>::max();
    record.oldValue = oldValue;
    record.newValue = newValue;
    record.description = note.empty() ? "manual_patch" : std::move(note);
    record.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    record.kind = ManualEventKind::CellEdit;
    eventQueue_.recordManualEvent(record);

    message = "manual_patch_queued variable=" + variableName;
    return true;
}

RuntimeEvent Runtime::buildUndoEvent(const ManualEventRecord& manualEvent) const {
    RuntimeEvent event;
    event.eventName = "undo_manual_patch";
    if (manualEvent.cellIndex == std::numeric_limits<std::uint64_t>::max()) {
        event.scalarPatches.reserve(static_cast<std::size_t>(config_.grid.cellCount()));
        for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
            for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                event.scalarPatches.push_back(ScalarWritePatch{manualEvent.variable, Cell{x, y}, manualEvent.oldValue});
            }
        }
        return event;
    }

    const Cell cell = stateStore_.cellFromIndex(manualEvent.cellIndex);
    event.scalarPatches.push_back(ScalarWritePatch{manualEvent.variable, cell, manualEvent.oldValue});
    return event;
}

bool Runtime::undoLastManualPatch(std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "manual_patch_undo_failed reason=runtime_not_running";
        return false;
    }
    if (!paused_) {
        message = "manual_patch_undo_failed reason=runtime_not_paused";
        return false;
    }

    ManualEventRecord previous;
    if (!eventQueue_.popLastManualEventOfKind(ManualEventKind::CellEdit, previous)) {
        message = "manual_patch_undo_failed reason=log_empty";
        return false;
    }

    enqueueEvent(buildUndoEvent(previous));
    message = "manual_patch_undo_queued variable=" + previous.variable;
    return true;
}

RuntimeEvent Runtime::buildPerturbationEvent(const PerturbationSpec& perturbation, const std::uint64_t appliedStep) const {
    RuntimeEvent event;
    event.eventName = "perturbation." + perturbation.targetVariable;
    if (!stateStore_.hasField(perturbation.targetVariable)) {
        return event;
    }

    const auto clampCell = [&](const std::int64_t x, const std::int64_t y) {
        return stateStore_.resolveBoundary(CellSigned{x, y});
    };

    switch (perturbation.type) {
        case PerturbationType::Gaussian: {
            const float sigma = std::max(0.5f, perturbation.sigma);
            const int radius = static_cast<int>(std::ceil(3.0f * sigma));
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const float dist2 = static_cast<float>(dx * dx + dy * dy);
                    const float weight = std::exp(-dist2 / (2.0f * sigma * sigma));
                    const Cell c = clampCell(
                        static_cast<std::int64_t>(perturbation.origin.x) + dx,
                        static_cast<std::int64_t>(perturbation.origin.y) + dy);
                    event.scalarPatches.push_back(ScalarWritePatch{perturbation.targetVariable, c, perturbation.amplitude * weight});
                }
            }
            break;
        }
        case PerturbationType::Rectangle: {
            for (std::uint32_t yy = 0; yy < std::max<std::uint32_t>(1u, perturbation.height); ++yy) {
                for (std::uint32_t xx = 0; xx < std::max<std::uint32_t>(1u, perturbation.width); ++xx) {
                    const Cell c = clampCell(
                        static_cast<std::int64_t>(perturbation.origin.x) + static_cast<std::int64_t>(xx),
                        static_cast<std::int64_t>(perturbation.origin.y) + static_cast<std::int64_t>(yy));
                    event.scalarPatches.push_back(ScalarWritePatch{perturbation.targetVariable, c, perturbation.amplitude});
                }
            }
            break;
        }
        case PerturbationType::Sine: {
            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    const float phase = perturbation.frequency * static_cast<float>(x + y) + 0.1f * static_cast<float>(appliedStep);
                    const float value = perturbation.amplitude * std::sin(phase);
                    event.scalarPatches.push_back(ScalarWritePatch{perturbation.targetVariable, Cell{x, y}, value});
                }
            }
            break;
        }
        case PerturbationType::WhiteNoise: {
            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    const std::uint64_t h0 = DeterministicHash::combine(perturbation.noiseSeed, DeterministicHash::hashPod(appliedStep));
                    const std::uint64_t h1 = DeterministicHash::combine(h0, DeterministicHash::hashPod(x));
                    const std::uint64_t h2 = DeterministicHash::combine(h1, DeterministicHash::hashPod(y));
                    const float centered = static_cast<float>((h2 >> 40u) & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) - 0.5f;
                    event.scalarPatches.push_back(ScalarWritePatch{perturbation.targetVariable, Cell{x, y}, centered * perturbation.amplitude * 2.0f});
                }
            }
            break;
        }
        case PerturbationType::Gradient: {
            const float denom = static_cast<float>(std::max<std::uint32_t>(1u, config_.grid.width - 1u));
            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    const float t = static_cast<float>(x) / denom;
                    event.scalarPatches.push_back(ScalarWritePatch{perturbation.targetVariable, Cell{x, y}, perturbation.amplitude * t});
                }
            }
            break;
        }
    }

    return event;
}

bool Runtime::enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "perturbation_enqueue_failed reason=runtime_not_running";
        return false;
    }
    if (!stateStore_.hasField(perturbation.targetVariable)) {
        message = "perturbation_enqueue_failed reason=unknown_variable variable=" + perturbation.targetVariable;
        return false;
    }

    pendingPerturbations_.push_back(perturbation);

    ManualEventRecord record;
    record.step = stateStore_.header().stepIndex;
    record.time = static_cast<float>(stateStore_.header().timestampTicks);
    record.variable = perturbation.targetVariable;
    record.cellIndex = std::numeric_limits<std::uint64_t>::max();
    record.oldValue = 0.0f;
    record.newValue = perturbation.amplitude;
    record.description = perturbation.description.empty() ? "perturbation" : perturbation.description;
    record.timestamp = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    record.kind = ManualEventKind::Perturbation;
    eventQueue_.recordManualEvent(record);

    message = "perturbation_enqueued target=" + perturbation.targetVariable;
    return true;
}

bool Runtime::addProbe(const ProbeDefinition& definition, std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "probe_add_failed reason=runtime_not_running";
        return false;
    }

    const bool ok = probeManager_.addProbe(definition, stateStore_, message);
    if (ok) {
        const auto& header = stateStore_.header();
        probeManager_.recordAll(stateStore_, header.stepIndex, static_cast<float>(header.timestampTicks));
    }
    return ok;
}

bool Runtime::removeProbe(const std::string& probeId, std::string& message) {
    if (status_ != RuntimeStatus::Running) {
        message = "probe_remove_failed reason=runtime_not_running";
        return false;
    }

    return probeManager_.removeProbe(probeId, message);
}

void Runtime::clearProbes() noexcept {
    probeManager_.clear();
}

std::uint64_t Runtime::computeStateHash() const noexcept {
    return stateStore_.stateHash();
}

bool Runtime::validateDeterminism(const std::vector<std::uint64_t>& referenceHashes) const noexcept {
    if (referenceHashes.size() != stateHashHistory_.size()) {
        return false;
    }

    for (std::size_t i = 0; i < referenceHashes.size(); ++i) {
        if (referenceHashes[i] != stateHashHistory_[i]) {
            return false;
        }
    }

    return true;
}

void Runtime::allocateRuntimeFieldsFromModelSpec() {
    std::uint32_t nextRuntimeVariableId = 1000u;

    if (config_.modelExecutionSpec.has_value()) {
        std::vector<std::string> variableNames = config_.modelExecutionSpec->cellScalarVariableIds;
        variableNames.erase(
            std::remove_if(variableNames.begin(), variableNames.end(), [](const std::string& name) {
                return name.empty();
            }),
            variableNames.end());
        std::sort(variableNames.begin(), variableNames.end());
        variableNames.erase(std::unique(variableNames.begin(), variableNames.end()), variableNames.end());

        for (const auto& variableName : variableNames) {
            if (stateStore_.hasField(variableName)) {
                continue;
            }
            stateStore_.allocateScalarField(VariableSpec{nextRuntimeVariableId++, variableName});
        }
    }

    for (const auto& subsystem : scheduler_.registeredSubsystems()) {
        if (!subsystem) {
            continue;
        }
        for (const auto& writeKey : subsystem->declaredWriteSet()) {
            if (writeKey.empty() || stateStore_.hasField(writeKey)) {
                continue;
            }
            if (writeKey.find('.') != std::string::npos) {
                continue;
            }
            stateStore_.allocateScalarField(VariableSpec{nextRuntimeVariableId++, writeKey});
        }
    }

    if (!stateStore_.hasField("seed_probe")) {
        stateStore_.allocateScalarField(VariableSpec{nextRuntimeVariableId++, "seed_probe"});
    }
}

std::uint64_t Runtime::applyInputFrame(const RuntimeInputFrame& inputFrame) {
    if (inputFrame.scalarPatches.empty()) {
        return 0;
    }

    StateStore::WriteSession inputWriter(
        stateStore_,
        "runtime.input_ingest",
        collectWritableVariables(inputFrame.scalarPatches));

    for (const auto& patch : inputFrame.scalarPatches) {
        inputWriter.setScalar(patch.variableName, patch.cell, patch.value);
    }

    return static_cast<std::uint64_t>(inputFrame.scalarPatches.size());
}

std::uint64_t Runtime::applyEvent(const RuntimeEvent& event, const std::uint64_t eventOrdinal) {
    if (event.scalarPatches.empty()) {
        return 0;
    }

    const std::string owner = event.eventName.empty()
        ? ("runtime.event_queue." + std::to_string(eventOrdinal))
        : ("runtime.event_queue." + event.eventName + "." + std::to_string(eventOrdinal));
    StateStore::WriteSession eventWriter(stateStore_, owner, collectWritableVariables(event.scalarPatches));

    for (const auto& patch : event.scalarPatches) {
        eventWriter.setScalar(patch.variableName, patch.cell, patch.value);
    }

    trace(
        TraceChannel::Event,
        "runtime.event.applied",
        owner,
        DeterministicHash::hashPod(eventOrdinal),
        stateStore_.header().stepIndex);

    return static_cast<std::uint64_t>(event.scalarPatches.size());
}

std::vector<std::string> Runtime::collectWritableVariables(const std::vector<ScalarWritePatch>& patches) {
    std::vector<std::string> variables;
    variables.reserve(patches.size());
    for (const auto& patch : patches) {
        variables.push_back(patch.variableName);
    }

    std::sort(variables.begin(), variables.end());
    variables.erase(std::unique(variables.begin(), variables.end()), variables.end());
    return variables;
}

std::string Runtime::stableHashForStringSet(const std::vector<std::string>& orderedValues) {
    std::vector<std::string> normalized = orderedValues;
    std::sort(normalized.begin(), normalized.end());
    std::string digest;
    for (const auto& value : normalized) {
        digest += value;
        digest += ';';
    }
    return std::to_string(DeterministicHash::hashString(digest));
}

const AdmissionReport& Runtime::admissionReport() const {
    if (!admissionReport_.admitted) {
        throw std::runtime_error("Runtime admission report is not available before successful start");
    }
    return admissionReport_;
}

void Runtime::trace(
    const TraceChannel channel,
    std::string name,
    std::string detail,
    const std::uint64_t payloadFingerprint,
    const std::uint64_t stepIndexOverride) {
    const std::uint64_t stepIndex =
        (stepIndexOverride == std::numeric_limits<std::uint64_t>::max())
            ? stateStore_.header().stepIndex
            : stepIndexOverride;
    observability_.record(TraceRecord{
        traceSequence_++,
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        stepIndex,
        channel,
        std::move(name),
        std::move(detail),
        payloadFingerprint});
}

} // namespace ws

