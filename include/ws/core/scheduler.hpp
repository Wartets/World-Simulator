#pragma once

// Core dependencies
#include "ws/core/interactions.hpp"
#include "ws/core/profile.hpp"
#include "ws/core/state_store.hpp"

// Standard library
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Subsystem Interface
// =============================================================================

// Interface for simulation subsystems that operate on the state store.
// Subsystems are executed in order based on their declared read/write sets.
class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    // Returns the name of this subsystem for logging and debugging.
    [[nodiscard]] virtual std::string name() const = 0;
    // Returns the set of variables this subsystem reads.
    [[nodiscard]] virtual std::vector<std::string> declaredReadSet() const = 0;
    // Returns the set of variables this subsystem writes.
    [[nodiscard]] virtual std::vector<std::string> declaredWriteSet() const = 0;
    // Initializes the subsystem with the state store and profile.
    virtual void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) = 0;
    // Called before each step to allow subsystem preparation.
    virtual void preStep(const ModelProfile& profile, std::uint64_t stepIndex);
    // Executes the subsystem's main computation for the current step.
    virtual void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) = 0;
    // Called after each step for post-processing and cleanup.
    virtual void postStep(const ModelProfile& profile, std::uint64_t stepIndex);
};

// =============================================================================
// Numeric Guardrail Policy
// =============================================================================

// Policy for numerical stability monitoring and enforcement.
// Controls clamping, delta limits, and adaptive sub-iteration behavior.
struct NumericGuardrailPolicy {
    float clampMin = -1000000.0f;
    float clampMax = 1000000.0f;
    float maxAbsDeltaPerStep = 1000.0f;
    bool clampEnabled = true;
    bool boundedIncrementEnabled = true;
    std::uint32_t multiRateMicroStepCount = 4;
    std::uint32_t minAdaptiveSubIterations = 1;
    std::uint32_t maxAdaptiveSubIterations = 4;
    float stiffnessDriftThreshold = 0.10f;
    float divergenceSoftLimit = 0.30f;
    float divergenceHardLimit = 0.75f;
    float dampingFactor = 0.35f;
    bool enableControlledFallback = true;
};

// =============================================================================
// Stability Diagnostics
// =============================================================================

// Diagnostics data from numerical stability monitoring.
struct StabilityDiagnostics {
    // Residual for a single conserved variable.
    struct ConservationResidual {
        std::string variableName;
        double residual = 0.0;
    };

    double driftMetric = 0.0;
    double amplificationIndicator = 1.0;
    double conservationResidualWater = 0.0;
    double conservationResidualResources = 0.0;
    std::vector<ConservationResidual> conservationResiduals;
    std::uint32_t microStepsExecuted = 0;
    std::uint32_t adaptiveSubIterations = 0;
    std::uint32_t dampingApplications = 0;
    std::uint32_t fallbackApplications = 0;
    EscalationAction finalEscalationAction = EscalationAction::None;
};

// =============================================================================
// Step Diagnostics
// =============================================================================

// Diagnostics collected during each simulation step execution.
struct StepDiagnostics {
    std::uint64_t inputPatchesApplied = 0;
    std::uint64_t eventPatchesApplied = 0;
    std::uint64_t eventsApplied = 0;
    std::uint64_t parallelBatchesExecuted = 0;
    std::uint64_t parallelTasksDispatched = 0;
    std::vector<std::string> orderingLog;
    std::vector<std::string> stabilityAlerts;
    std::vector<std::string> constraintViolations;
    ReproducibilityClass reproducibilityClass = ReproducibilityClass::Strict;
    ExecutionPolicyMode executionPolicyMode = ExecutionPolicyMode::StrictDeterministic;
    StabilityDiagnostics stability;
};

// =============================================================================
// Scheduler Class
// =============================================================================

// Manages subsystem execution order based on data dependency analysis.
// Validates that the subsystem graph is a DAG and resolves write conflicts.
class Scheduler {
public:
    // Sets the execution policy mode for the scheduler.
    void setExecutionPolicyMode(ExecutionPolicyMode mode) noexcept;
    // Returns the current execution policy mode.
    [[nodiscard]] ExecutionPolicyMode executionPolicyMode() const noexcept;
    // Registers a subsystem to be executed during simulation steps.
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    // Returns the list of all registered subsystems.
    [[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> registeredSubsystems() const;
    // Sets the admission report from profile resolution.
    void setAdmissionReport(AdmissionReport report);
    // Returns the current admission report if set.
    [[nodiscard]] const std::optional<AdmissionReport>& admissionReport() const noexcept;
    // Initializes the scheduler and all registered subsystems.
    void initialize(StateStore& stateStore, const ModelProfile& profile);
    // Executes one simulation step, calling all subsystems in order.
    [[nodiscard]] StepDiagnostics step(
        StateStore& stateStore,
        const ModelProfile& profile,
        TemporalPolicy temporalPolicy,
        const NumericGuardrailPolicy& guardrailPolicy,
        std::uint64_t stepIndex);

    // Returns the names of all active (registered and initialized) subsystems.
    [[nodiscard]] std::vector<std::string> activeSubsystemNames() const;
    // Returns a map from variable names to their owning subsystem names.
    [[nodiscard]] std::map<std::string, std::string, std::less<>> writeOwnershipByVariable() const;
    // Returns the observed data flow between subsystems from access tracking.
    [[nodiscard]] const std::map<std::string, AccessObservation, std::less<>>& observedDataFlow() const noexcept;
    // Validates that the observed data flow during execution matches declared dependencies.
    void validateObservedDataFlow() const;

private:
    // Builds the execution order based on dependency analysis.
    [[nodiscard]] std::vector<std::string> buildExecutionOrder() const;
    // Validates that the subsystem graph is a valid DAG.
    void validateDAG() const;
    // Validates that no two subsystems write to the same variable.
    void validateWriteOwnership() const;
    // Returns subsystems in execution order.
    [[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> orderedSubsystems() const;
    // Computes the effective write set for a subsystem.
    [[nodiscard]] std::set<std::string, std::less<>> effectiveWriteSetFor(const std::shared_ptr<ISubsystem>& subsystem) const;
    // Resolves the runtime write set by checking field aliases.
    [[nodiscard]] std::set<std::string, std::less<>> resolveRuntimeWriteSet(
        const std::shared_ptr<ISubsystem>& subsystem,
        const StateStore& stateStore) const;
    // Attaches an access observer to track subsystem data flow.
    void attachObserverForSubsystem(StateStore& stateStore, const std::shared_ptr<ISubsystem>& subsystem);

    std::vector<std::shared_ptr<ISubsystem>> subsystems_;
    ExecutionPolicyMode executionPolicyMode_ = ExecutionPolicyMode::StrictDeterministic;
    std::optional<AdmissionReport> admissionReport_;
    std::map<std::string, AccessObservation, std::less<>> observedDataFlow_;
};

} // namespace ws
