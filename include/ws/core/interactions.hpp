#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/types.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ws {

class ISubsystem;

enum class DependencyEdgeKind : std::uint8_t {
    Data = 0,
    TemporalRate = 1
};

enum class ConflictResolutionMode : std::uint8_t {
    DeterministicPriority = 0,
    MergeSum = 1,
    MergeAverage = 2
};

struct DependencyNode {
    std::string subsystemName;
    ModelTier tier = ModelTier::A;
};

struct DependencyEdge {
    std::string fromSubsystem;
    std::string toSubsystem;
    std::string variableName;
    DependencyEdgeKind kind = DependencyEdgeKind::Data;
};

struct AccessObservation {
    std::set<std::string, std::less<>> reads;
    std::set<std::string, std::less<>> writes;
};

struct SubsystemContractReport {
    std::string subsystemName;
    std::set<std::string, std::less<>> declaredReads;
    std::set<std::string, std::less<>> declaredWrites;
};

struct ConflictResolution {
    std::string variableName;
    ConflictResolutionMode mode = ConflictResolutionMode::DeterministicPriority;
    std::vector<std::string> writersInPriorityOrder;
    std::string selectedWriter;
};

struct AdmissionIssue {
    enum class Severity : std::uint8_t {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    Severity severity = Severity::Info;
    std::string code;
    std::string message;
};

struct AdmissionReport {
    bool admitted = false;
    std::uint64_t fingerprint = 0;
    ReproducibilityClass reproducibilityClass = ReproducibilityClass::Strict;
    double reproducibilityTolerance = 0.0;
    std::string serializedGraph;
    std::vector<std::string> deterministicOrder;
    std::vector<DependencyNode> nodes;
    std::vector<DependencyEdge> edges;
    std::vector<ConflictResolution> conflicts;
    std::vector<AdmissionIssue> issues;
    std::vector<SubsystemContractReport> subsystemContracts;

    [[nodiscard]] std::string diagnosticsText() const;
};

class InteractionCoordinator {
public:
    [[nodiscard]] AdmissionReport buildAdmissionReport(
        const ModelProfile& profile,
        TemporalPolicy temporalPolicy,
        const std::vector<std::shared_ptr<ISubsystem>>& subsystems) const;

    static void validateObservedDataFlow(
        const AdmissionReport& report,
        const std::map<std::string, AccessObservation, std::less<>>& observedBySubsystem);
};

} // namespace ws
