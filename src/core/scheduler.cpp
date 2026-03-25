#include "ws/core/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace ws {

void ISubsystem::preStep(const ModelProfile&, const std::uint64_t) {}

void ISubsystem::postStep(const ModelProfile&, const std::uint64_t) {}

namespace {

bool matchesPhase(const ModelTier tier, const std::uint32_t phaseIndex) {
    if (phaseIndex == 0u) {
        return tier == ModelTier::A;
    }
    return tier == ModelTier::B || tier == ModelTier::C;
}

} // namespace

void Scheduler::registerSubsystem(std::shared_ptr<ISubsystem> subsystem) {
    if (!subsystem) {
        throw std::invalid_argument("Cannot register null subsystem");
    }
    subsystems_.push_back(std::move(subsystem));
    std::stable_sort(
        subsystems_.begin(),
        subsystems_.end(),
        [](const std::shared_ptr<ISubsystem>& left, const std::shared_ptr<ISubsystem>& right) {
            return left->name() < right->name();
        });
}

void Scheduler::initialize(StateStore& stateStore, const ModelProfile& profile) {
    for (const auto& subsystem : subsystems_) {
        StateStore::WriteSession session(stateStore, subsystem->name(), subsystem->declaredWriteSet());
        subsystem->initialize(session, profile);
    }
}

StepDiagnostics Scheduler::step(
    StateStore& stateStore,
    const ModelProfile& profile,
    const TemporalPolicy temporalPolicy,
    const NumericGuardrailPolicy& guardrailPolicy,
    const std::uint64_t stepIndex) {
    if (guardrailPolicy.clampEnabled && guardrailPolicy.clampMin > guardrailPolicy.clampMax) {
        throw std::invalid_argument("Scheduler guardrail clamp range is invalid");
    }
    if (guardrailPolicy.boundedIncrementEnabled && guardrailPolicy.maxAbsDeltaPerStep < 0.0f) {
        throw std::invalid_argument("Scheduler maxAbsDeltaPerStep must be non-negative");
    }

    StepDiagnostics diagnostics;
    diagnostics.orderingLog.push_back("pipeline:begin_step_contracts");

    for (const auto& subsystem : subsystems_) {
        subsystem->preStep(profile, stepIndex);
    }

    std::unordered_map<std::string, std::vector<float>> preStepValues;
    for (const auto& variableName : stateStore.variableNames()) {
        const auto& field = stateStore.scalarField(variableName);
        const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
        preStepValues.emplace(variableName, std::vector<float>(field.begin(), field.begin() + static_cast<std::ptrdiff_t>(logicalCount)));
    }

    diagnostics.orderingLog.push_back("pipeline:subsystem_updates");

    if (temporalPolicy == TemporalPolicy::UniformA) {
        for (const auto& subsystem : subsystems_) {
            diagnostics.orderingLog.push_back("update:" + subsystem->name());
            StateStore::WriteSession session(stateStore, subsystem->name(), subsystem->declaredWriteSet());
            subsystem->step(session, profile, stepIndex);
        }
    } else if (temporalPolicy == TemporalPolicy::PhasedB) {
        for (std::uint32_t phase = 0; phase < 2; ++phase) {
            for (const auto& subsystem : subsystems_) {
                const auto tierIt = profile.subsystemTiers.find(subsystem->name());
                const ModelTier tier = tierIt == profile.subsystemTiers.end() ? ModelTier::A : tierIt->second;

                if (!matchesPhase(tier, phase)) {
                    continue;
                }

                diagnostics.orderingLog.push_back("phase" + std::to_string(phase) + ":update:" + subsystem->name());
                StateStore::WriteSession session(stateStore, subsystem->name(), subsystem->declaredWriteSet());
                subsystem->step(session, profile, stepIndex);
            }
        }
    } else {
        throw std::runtime_error("Scheduler TemporalPolicy::MultiRateC is not available in Phase 3 scope");
    }

    diagnostics.orderingLog.push_back("pipeline:constraint_pass");

    StateStore::WriteSession constraintSession(stateStore, "constraint_pass", stateStore.variableNames());
    for (const auto& variableName : stateStore.variableNames()) {
        const auto& postValues = stateStore.scalarField(variableName);
        const auto& preValues = preStepValues.at(variableName);
        const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));

        for (std::size_t i = 0; i < logicalCount; ++i) {
            const float currentValue = postValues[i];

            if (!std::isfinite(currentValue)) {
                std::ostringstream error;
                error << "Numerical fail-fast: non-finite value for variable='" << variableName << "' index=" << i;
                throw std::runtime_error(error.str());
            }

            float adjusted = currentValue;
            if (guardrailPolicy.clampEnabled) {
                const float clamped = std::clamp(adjusted, guardrailPolicy.clampMin, guardrailPolicy.clampMax);
                if (clamped != adjusted) {
                    diagnostics.constraintViolations.push_back(
                        "clamp:" + variableName + ":index=" + std::to_string(i));
                    adjusted = clamped;
                }
            }

            if (guardrailPolicy.boundedIncrementEnabled) {
                const float previous = preValues[i];
                const float delta = adjusted - previous;
                if (std::fabs(delta) > guardrailPolicy.maxAbsDeltaPerStep) {
                    const float signedLimit = (delta < 0.0f ? -guardrailPolicy.maxAbsDeltaPerStep : guardrailPolicy.maxAbsDeltaPerStep);
                    adjusted = previous + signedLimit;
                    diagnostics.stabilityAlerts.push_back(
                        "bounded_increment:" + variableName + ":index=" + std::to_string(i));
                }
            }

            if (adjusted != currentValue) {
                constraintSession.setScalar(variableName, stateStore.cellFromIndex(static_cast<std::uint64_t>(i)), adjusted);
            }
        }
    }

    diagnostics.orderingLog.push_back("pipeline:commit_step");
    for (const auto& subsystem : subsystems_) {
        subsystem->postStep(profile, stepIndex);
    }

    return diagnostics;
}

std::vector<std::string> Scheduler::activeSubsystemNames() const {
    std::vector<std::string> names;
    names.reserve(subsystems_.size());
    for (const auto& subsystem : subsystems_) {
        names.push_back(subsystem->name());
    }
    return names;
}

} // namespace ws
