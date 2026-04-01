#include "ws/core/probe.hpp"

#include <algorithm>
#include <cmath>

namespace ws {

bool ProbeManager::addProbe(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message) {
    if (definition.id.empty()) {
        message = "probe_add_failed reason=empty_id";
        return false;
    }

    if (probes_.find(definition.id) != probes_.end()) {
        message = "probe_add_failed reason=duplicate_id id=" + definition.id;
        return false;
    }

    if (!validateDefinition(definition, stateStore, message)) {
        return false;
    }

    ProbeSeries series;
    series.definition = definition;
    probes_.emplace(definition.id, std::move(series));
    message = "probe_added id=" + definition.id;
    return true;
}

bool ProbeManager::removeProbe(const std::string& probeId, std::string& message) {
    const auto erased = probes_.erase(probeId);
    if (erased == 0u) {
        message = "probe_remove_failed reason=unknown_id id=" + probeId;
        return false;
    }

    message = "probe_removed id=" + probeId;
    return true;
}

void ProbeManager::clear() noexcept {
    probes_.clear();
}

void ProbeManager::clearSamples() noexcept {
    for (auto& [_, series] : probes_) {
        series.samples.clear();
    }
}

void ProbeManager::recordAll(const StateStore& stateStore, const std::uint64_t step, const float time) {
    for (auto& [_, series] : probes_) {
        const auto sampled = sample(series.definition, stateStore);
        if (!sampled.has_value() || !std::isfinite(*sampled)) {
            continue;
        }

        series.samples.push_back(ProbeSample{step, time, *sampled});
    }
}

std::vector<ProbeDefinition> ProbeManager::definitions() const {
    std::vector<ProbeDefinition> result;
    result.reserve(probes_.size());
    for (const auto& [_, series] : probes_) {
        result.push_back(series.definition);
    }
    return result;
}

bool ProbeManager::getSeries(const std::string& probeId, ProbeSeries& outSeries, std::string& message) const {
    const auto it = probes_.find(probeId);
    if (it == probes_.end()) {
        message = "probe_series_failed reason=unknown_id id=" + probeId;
        return false;
    }

    outSeries = it->second;
    message = "probe_series_ready id=" + probeId + " samples=" + std::to_string(outSeries.samples.size());
    return true;
}

std::vector<ProbeSeries> ProbeManager::allSeries() const {
    std::vector<ProbeSeries> result;
    result.reserve(probes_.size());
    for (const auto& [_, series] : probes_) {
        result.push_back(series);
    }
    return result;
}

ProbeStatistics ProbeManager::computeStatistics(const ProbeSeries& series) {
    ProbeStatistics stats;
    if (series.samples.empty()) {
        return stats;
    }

    stats.count = series.samples.size();
    stats.minValue = series.samples.front().value;
    stats.maxValue = series.samples.front().value;
    stats.lastValue = series.samples.back().value;

    double sum = 0.0;
    for (const auto& sampleValue : series.samples) {
        stats.minValue = std::min(stats.minValue, sampleValue.value);
        stats.maxValue = std::max(stats.maxValue, sampleValue.value);
        sum += static_cast<double>(sampleValue.value);
    }

    stats.mean = sum / static_cast<double>(stats.count);

    double variance = 0.0;
    for (const auto& sampleValue : series.samples) {
        const double delta = static_cast<double>(sampleValue.value) - stats.mean;
        variance += delta * delta;
    }

    variance /= static_cast<double>(stats.count);
    stats.stddev = std::sqrt(std::max(0.0, variance));
    return stats;
}

bool ProbeManager::validateDefinition(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message) {
    if (!stateStore.hasField(definition.variableName)) {
        message = "probe_add_failed reason=unknown_variable variable=" + definition.variableName;
        return false;
    }

    const auto& grid = stateStore.grid();
    const auto inBounds = [&grid](const Cell cell) {
        return cell.x < grid.width && cell.y < grid.height;
    };

    switch (definition.kind) {
        case ProbeKind::GlobalScalar:
            return true;
        case ProbeKind::CellScalar:
            if (!inBounds(definition.cell)) {
                message = "probe_add_failed reason=cell_out_of_bounds";
                return false;
            }
            return true;
        case ProbeKind::RegionAverage:
            if (!inBounds(definition.region.min) || !inBounds(definition.region.max)) {
                message = "probe_add_failed reason=region_out_of_bounds";
                return false;
            }
            if (definition.region.min.x > definition.region.max.x || definition.region.min.y > definition.region.max.y) {
                message = "probe_add_failed reason=invalid_region_bounds";
                return false;
            }
            return true;
    }

    message = "probe_add_failed reason=invalid_kind";
    return false;
}

std::optional<float> ProbeManager::sample(const ProbeDefinition& definition, const StateStore& stateStore) {
    switch (definition.kind) {
        case ProbeKind::GlobalScalar: {
            return stateStore.trySampleScalar(definition.variableName, CellSigned{0, 0});
        }
        case ProbeKind::CellScalar: {
            return stateStore.trySampleScalar(
                definition.variableName,
                CellSigned{static_cast<std::int64_t>(definition.cell.x), static_cast<std::int64_t>(definition.cell.y)});
        }
        case ProbeKind::RegionAverage: {
            double sum = 0.0;
            std::size_t count = 0;
            for (std::uint32_t y = definition.region.min.y; y <= definition.region.max.y; ++y) {
                for (std::uint32_t x = definition.region.min.x; x <= definition.region.max.x; ++x) {
                    const auto value = stateStore.trySampleScalar(
                        definition.variableName,
                        CellSigned{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)});
                    if (!value.has_value() || !std::isfinite(*value)) {
                        continue;
                    }

                    sum += static_cast<double>(*value);
                    count += 1;
                }
            }

            if (count == 0u) {
                return std::nullopt;
            }

            return static_cast<float>(sum / static_cast<double>(count));
        }
    }

    return std::nullopt;
}

} // namespace ws
