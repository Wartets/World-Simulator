#pragma once

#include "ws/core/state_store.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ws {

enum class ProbeKind : std::uint8_t {
    GlobalScalar = 0,
    CellScalar = 1,
    RegionAverage = 2
};

struct ProbeRegion {
    Cell min{0, 0};
    Cell max{0, 0};
};

struct ProbeDefinition {
    std::string id;
    std::string variableName;
    ProbeKind kind = ProbeKind::GlobalScalar;
    Cell cell{0, 0};
    ProbeRegion region{};
};

struct ProbeSample {
    std::uint64_t step = 0;
    float time = 0.0f;
    float value = 0.0f;
};

struct ProbeSeries {
    ProbeDefinition definition{};
    std::vector<ProbeSample> samples;
};

struct ProbeStatistics {
    std::size_t count = 0;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double mean = 0.0;
    double stddev = 0.0;
    float lastValue = 0.0f;
};

class ProbeManager {
public:
    [[nodiscard]] bool addProbe(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message);
    [[nodiscard]] bool removeProbe(const std::string& probeId, std::string& message);
    void clear() noexcept;
    void clearSamples() noexcept;

    void recordAll(const StateStore& stateStore, std::uint64_t step, float time);

    [[nodiscard]] std::vector<ProbeDefinition> definitions() const;
    [[nodiscard]] bool getSeries(const std::string& probeId, ProbeSeries& outSeries, std::string& message) const;
    [[nodiscard]] std::vector<ProbeSeries> allSeries() const;

    [[nodiscard]] static ProbeStatistics computeStatistics(const ProbeSeries& series);

private:
    [[nodiscard]] static bool validateDefinition(const ProbeDefinition& definition, const StateStore& stateStore, std::string& message);
    [[nodiscard]] static std::optional<float> sample(const ProbeDefinition& definition, const StateStore& stateStore);

    std::map<std::string, ProbeSeries, std::less<>> probes_;
};

} // namespace ws
