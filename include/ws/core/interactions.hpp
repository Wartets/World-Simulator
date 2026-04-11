#pragma once

// Core dependencies
#include "ws/core/profile.hpp"
#include "ws/core/types.hpp"

// Standard library
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ws {

// Forward declaration
class ISubsystem;

// =============================================================================
// Dependency Edge Kind
// =============================================================================

// Classification of dependencies between subsystems.
enum class DependencyEdgeKind : std::uint8_t {
    Data = 0,
    TemporalRate = 1
};

// =============================================================================
// Conflict Resolution Mode
// =============================================================================

// Strategy for resolving write conflicts between subsystems.
enum class ConflictResolutionMode : std::uint8_t {
    DeterministicPriority = 0,
    MergeSum = 1,
    MergeAverage = 2
};

// =============================================================================
// Dependency Node
// =============================================================================

// Node in the subsystem dependency graph.
struct DependencyNode {
    std::string subsystemName;
    ModelTier tier = ModelTier::Minimal;
};

// =============================================================================
// Dependency Edge
// =============================================================================

// Edge in the subsystem dependency graph representing data flow.
struct DependencyEdge {
    std::string fromSubsystem;
    std::string toSubsystem;
    std::string variableName;
    DependencyEdgeKind kind = DependencyEdgeKind::Data;
};

// =============================================================================
// Access Observation
// =============================================================================

// Records the variables read and written by a subsystem during execution.
struct AccessObservation {
    std::set<std::string, std::less<>> reads;
    std::set<std::string, std::less<>> writes;
};

// =============================================================================
// Subsystem Contract Report
// =============================================================================

// Contract declaration from a subsystem for dependency analysis.
struct SubsystemContractReport {
    std::string subsystemName;
    std::set<std::string, std::less<>> declaredReads;
    std::set<std::string, std::less<>> declaredWrites;
};

// =============================================================================
// Conflict Resolution
// =============================================================================

// Resolution strategy for a write-write conflict on a variable.
struct ConflictResolution {
    std::string variableName;
    ConflictResolutionMode mode = ConflictResolutionMode::DeterministicPriority;
    std::vector<std::string> writersInPriorityOrder;
    std::string selectedWriter;
};

// =============================================================================
// Admission Issue
// =============================================================================

// Issue detected during model admission and dependency analysis.
struct AdmissionIssue {
    // Severity level for the issue.
    enum class Severity : std::uint8_t {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    Severity severity = Severity::Info;
    std::string code;
    std::string message;
};

// =============================================================================
// Admission Report
// =============================================================================

// Complete report from the admission and dependency analysis process.
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

    // Generates human-readable diagnostics text.
    [[nodiscard]] std::string diagnosticsText() const;
};

// =============================================================================
// Interaction Coordinator
// =============================================================================

// Coordinates subsystem interactions and performs dependency analysis.
class InteractionCoordinator {
public:
    // Builds an admission report by analyzing subsystem dependencies.
    [[nodiscard]] AdmissionReport buildAdmissionReport(
        const ModelProfile& profile,
        TemporalPolicy temporalPolicy,
        const std::vector<std::shared_ptr<ISubsystem>>& subsystems) const;

    // Validates that observed data flow matches declared contracts.
    static void validateObservedDataFlow(
        const AdmissionReport& report,
        const std::map<std::string, AccessObservation, std::less<>>& observedBySubsystem);
};

} // namespace ws
