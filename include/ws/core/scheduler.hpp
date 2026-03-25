#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/state_store.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ws {

class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<std::string> declaredWriteSet() const = 0;
    virtual void initialize(StateStore::WriteSession& writeSession, const ModelProfile& profile) = 0;
    virtual void preStep(const ModelProfile& profile, std::uint64_t stepIndex);
    virtual void step(StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) = 0;
    virtual void postStep(const ModelProfile& profile, std::uint64_t stepIndex);
};

struct NumericGuardrailPolicy {
    float clampMin = -1000000.0f;
    float clampMax = 1000000.0f;
    float maxAbsDeltaPerStep = 1000.0f;
    bool clampEnabled = true;
    bool boundedIncrementEnabled = true;
};

struct StepDiagnostics {
    std::uint64_t inputPatchesApplied = 0;
    std::uint64_t eventPatchesApplied = 0;
    std::uint64_t eventsApplied = 0;
    std::vector<std::string> orderingLog;
    std::vector<std::string> stabilityAlerts;
    std::vector<std::string> constraintViolations;
};

class Scheduler {
public:
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    void initialize(StateStore& stateStore, const ModelProfile& profile);
    [[nodiscard]] StepDiagnostics step(
        StateStore& stateStore,
        const ModelProfile& profile,
        TemporalPolicy temporalPolicy,
        const NumericGuardrailPolicy& guardrailPolicy,
        std::uint64_t stepIndex);

    [[nodiscard]] std::vector<std::string> activeSubsystemNames() const;

private:
    std::vector<std::shared_ptr<ISubsystem>> subsystems_;
};

} // namespace ws
