#pragma once

#include "ws/core/interactions.hpp"
#include "ws/core/profile.hpp"
#include "ws/core/state_store.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ws {

class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<std::string> declaredReadSet() const = 0;
    [[nodiscard]] virtual std::vector<std::string> declaredWriteSet() const = 0;
    virtual void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) = 0;
    virtual void preStep(const ModelProfile& profile, std::uint64_t stepIndex);
    virtual void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) = 0;
    virtual void postStep(const ModelProfile& profile, std::uint64_t stepIndex);
};

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

struct StabilityDiagnostics {
    double driftMetric = 0.0;
    double amplificationIndicator = 1.0;
    double conservationResidualWater = 0.0;
    double conservationResidualResources = 0.0;
    std::uint32_t microStepsExecuted = 0;
    std::uint32_t adaptiveSubIterations = 0;
    std::uint32_t dampingApplications = 0;
    std::uint32_t fallbackApplications = 0;
    EscalationAction finalEscalationAction = EscalationAction::None;
};

struct StepDiagnostics {
    std::uint64_t inputPatchesApplied = 0;
    std::uint64_t eventPatchesApplied = 0;
    std::uint64_t eventsApplied = 0;
    std::vector<std::string> orderingLog;
    std::vector<std::string> stabilityAlerts;
    std::vector<std::string> constraintViolations;
    ReproducibilityClass reproducibilityClass = ReproducibilityClass::Strict;
    StabilityDiagnostics stability;
};

class Scheduler {
public:
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    [[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> registeredSubsystems() const;
    void setAdmissionReport(AdmissionReport report);
    [[nodiscard]] const std::optional<AdmissionReport>& admissionReport() const noexcept;
    void initialize(StateStore& stateStore, const ModelProfile& profile);
    [[nodiscard]] StepDiagnostics step(
        StateStore& stateStore,
        const ModelProfile& profile,
        TemporalPolicy temporalPolicy,
        const NumericGuardrailPolicy& guardrailPolicy,
        std::uint64_t stepIndex);

    [[nodiscard]] std::vector<std::string> activeSubsystemNames() const;
    [[nodiscard]] std::map<std::string, std::string, std::less<>> writeOwnershipByVariable() const;
    [[nodiscard]] const std::map<std::string, AccessObservation, std::less<>>& observedDataFlow() const noexcept;
    void validateObservedDataFlow() const;

private:
    void validateWriteOwnership() const;
    [[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> orderedSubsystems() const;
    [[nodiscard]] std::set<std::string, std::less<>> effectiveWriteSetFor(const std::shared_ptr<ISubsystem>& subsystem) const;
    void attachObserverForSubsystem(StateStore& stateStore, const std::shared_ptr<ISubsystem>& subsystem);

    std::vector<std::shared_ptr<ISubsystem>> subsystems_;
    std::optional<AdmissionReport> admissionReport_;
    std::map<std::string, AccessObservation, std::less<>> observedDataFlow_;
};

} // namespace ws
