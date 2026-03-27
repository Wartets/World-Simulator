#include "ws/core/scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
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

ReproducibilityClass classifyReproducibility(const ModelProfile& profile) {
    std::size_t cTierCount = 0;
    for (const auto& [subsystemName, tier] : profile.subsystemTiers) {
        if (subsystemName == "temporal") {
            continue;
        }
        if (tier == ModelTier::C) {
            cTierCount += 1;
        }
    }

    if (cTierCount == 0) {
        return ReproducibilityClass::Strict;
    }
    if (cTierCount <= 3) {
        return ReproducibilityClass::BoundedDivergence;
    }
    return ReproducibilityClass::Exploratory;
}

using FieldSnapshot = std::unordered_map<std::string, std::vector<float>>;

FieldSnapshot captureFieldSnapshot(StateStore& stateStore) {
    FieldSnapshot snapshot;
    for (const auto& variableName : stateStore.variableNames()) {
        const auto& field = stateStore.scalarField(variableName);
        const auto logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
        snapshot.emplace(variableName, std::vector<float>(field.begin(), field.begin() + static_cast<std::ptrdiff_t>(logicalCount)));
    }
    return snapshot;
}

void restoreFieldSnapshot(StateStore& stateStore, const FieldSnapshot& snapshot, const std::string& owner) {
    StateStore::WriteSession restoreSession(stateStore, owner, stateStore.variableNames());
    for (const auto& variableName : stateStore.variableNames()) {
        const auto snapshotIt = snapshot.find(variableName);
        if (snapshotIt == snapshot.end()) {
            throw std::runtime_error("Missing restore snapshot variable: " + variableName);
        }
        const auto& values = snapshotIt->second;
        for (std::size_t i = 0; i < values.size(); ++i) {
            restoreSession.setScalar(variableName, stateStore.cellFromIndex(static_cast<std::uint64_t>(i)), values[i]);
        }
    }
}

double computeDriftMetric(StateStore& stateStore, const FieldSnapshot& reference) {
    double totalAbsDelta = 0.0;
    std::uint64_t sampleCount = 0;

    for (const auto& variableName : stateStore.variableNames()) {
        const auto& current = stateStore.scalarField(variableName);
        const auto referenceIt = reference.find(variableName);
        if (referenceIt == reference.end()) {
            throw std::runtime_error("Missing drift reference for variable: " + variableName);
        }
        const auto& previous = referenceIt->second;
        const auto logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
        for (std::size_t i = 0; i < logicalCount; ++i) {
            totalAbsDelta += std::fabs(static_cast<double>(current[i]) - static_cast<double>(previous[i]));
            sampleCount += 1;
        }
    }

    if (sampleCount == 0) {
        return 0.0;
    }
    return totalAbsDelta / static_cast<double>(sampleCount);
}

double computeTotal(StateStore& stateStore, const std::string& variableName) {
    const auto& field = stateStore.scalarField(variableName);
    const auto logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
    double total = 0.0;
    for (std::size_t i = 0; i < logicalCount; ++i) {
        total += static_cast<double>(field[i]);
    }
    return total;
}

void applyDampingFromReference(StateStore& stateStore, const FieldSnapshot& reference, const float dampingFactor) {
    StateStore::WriteSession dampingWriter(stateStore, "stability_damping", stateStore.variableNames());
    for (const auto& variableName : stateStore.variableNames()) {
        const auto& current = stateStore.scalarField(variableName);
        const auto referenceIt = reference.find(variableName);
        if (referenceIt == reference.end()) {
            throw std::runtime_error("Missing damping reference for variable: " + variableName);
        }
        const auto& prior = referenceIt->second;
        const auto logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
        for (std::size_t i = 0; i < logicalCount; ++i) {
            const float adjusted = prior[i] + dampingFactor * (current[i] - prior[i]);
            dampingWriter.setScalar(variableName, stateStore.cellFromIndex(static_cast<std::uint64_t>(i)), adjusted);
        }
    }
}

struct ParallelWriteBundle {
    std::string subsystemName;
    std::map<std::string, std::vector<float>, std::less<>> scalarWrites;
    std::set<std::string, std::less<>> observedReads;
    std::set<std::string, std::less<>> observedWrites;
};

std::vector<std::vector<std::string>> buildExecutionBatches(
    const AdmissionReport& report,
    const std::vector<std::shared_ptr<ISubsystem>>& orderedSubsystems) {
    std::map<std::string, std::size_t, std::less<>> orderIndex;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> adjacency;
    std::map<std::string, std::uint64_t, std::less<>> indegree;

    std::size_t index = 0;
    for (const auto& subsystem : orderedSubsystems) {
        orderIndex.emplace(subsystem->name(), index++);
        adjacency[subsystem->name()] = {};
        indegree[subsystem->name()] = 0;
    }

    for (const auto& edge : report.edges) {
        if (!orderIndex.contains(edge.fromSubsystem) || !orderIndex.contains(edge.toSubsystem)) {
            continue;
        }
        if (edge.fromSubsystem == edge.toSubsystem) {
            continue;
        }

        if (adjacency[edge.fromSubsystem].insert(edge.toSubsystem).second) {
            indegree[edge.toSubsystem] += 1;
        }
    }

    std::vector<std::vector<std::string>> batches;
    std::vector<std::string> frontier;
    for (const auto& [name, degree] : indegree) {
        if (degree == 0) {
            frontier.push_back(name);
        }
    }

    std::size_t processed = 0;
    while (!frontier.empty()) {
        std::sort(frontier.begin(), frontier.end(), [&](const std::string& left, const std::string& right) {
            return orderIndex.at(left) < orderIndex.at(right);
        });

        batches.push_back(frontier);
        processed += frontier.size();

        std::vector<std::string> nextFrontier;
        for (const auto& current : frontier) {
            for (const auto& next : adjacency.at(current)) {
                auto& degree = indegree[next];
                degree -= 1;
                if (degree == 0) {
                    nextFrontier.push_back(next);
                }
            }
        }
        frontier = std::move(nextFrontier);
    }

    if (processed != orderedSubsystems.size()) {
        return {};
    }

    return batches;
}

} // namespace

void Scheduler::setExecutionPolicyMode(const ExecutionPolicyMode mode) noexcept {
    executionPolicyMode_ = mode;
}

ExecutionPolicyMode Scheduler::executionPolicyMode() const noexcept {
    return executionPolicyMode_;
}

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

std::vector<std::shared_ptr<ISubsystem>> Scheduler::registeredSubsystems() const {
    return subsystems_;
}

void Scheduler::setAdmissionReport(AdmissionReport report) {
    admissionReport_ = std::move(report);
}

const std::optional<AdmissionReport>& Scheduler::admissionReport() const noexcept {
    return admissionReport_;
}

const std::map<std::string, AccessObservation, std::less<>>& Scheduler::observedDataFlow() const noexcept {
    return observedDataFlow_;
}

std::vector<std::shared_ptr<ISubsystem>> Scheduler::orderedSubsystems() const {
    if (!admissionReport_ || admissionReport_->deterministicOrder.empty()) {
        return subsystems_;
    }

    std::map<std::string, std::shared_ptr<ISubsystem>, std::less<>> byName;
    for (const auto& subsystem : subsystems_) {
        byName.emplace(subsystem->name(), subsystem);
    }

    std::vector<std::shared_ptr<ISubsystem>> ordered;
    ordered.reserve(subsystems_.size());

    std::set<std::string, std::less<>> seen;
    for (const auto& name : admissionReport_->deterministicOrder) {
        const auto it = byName.find(name);
        if (it == byName.end()) {
            continue;
        }
        ordered.push_back(it->second);
        seen.insert(name);
    }

    for (const auto& subsystem : subsystems_) {
        if (!seen.contains(subsystem->name())) {
            ordered.push_back(subsystem);
        }
    }

    return ordered;
}

std::set<std::string, std::less<>> Scheduler::effectiveWriteSetFor(const std::shared_ptr<ISubsystem>& subsystem) const {
    const auto declaredWrites = subsystem->declaredWriteSet();
    std::set<std::string, std::less<>> effective(
        declaredWrites.begin(),
        declaredWrites.end());

    if (!admissionReport_) {
        return effective;
    }

    for (const auto& conflict : admissionReport_->conflicts) {
        if (conflict.mode != ConflictResolutionMode::DeterministicPriority) {
            continue;
        }

        if (conflict.selectedWriter.empty()) {
            continue;
        }

        if (subsystem->name() != conflict.selectedWriter) {
            effective.erase(conflict.variableName);
        }
    }

    return effective;
}

void Scheduler::attachObserverForSubsystem(StateStore& stateStore, const std::shared_ptr<ISubsystem>& subsystem) {
    stateStore.setAccessObserver([this, subsystemName = subsystem->name()](const std::string& variableName, const StateStore::AccessKind kind) {
        auto& observation = observedDataFlow_[subsystemName];
        if (kind == StateStore::AccessKind::Read) {
            observation.reads.insert(variableName);
            return;
        }
        observation.writes.insert(variableName);
    });
}

void Scheduler::validateWriteOwnership() const {
    if (admissionReport_) {
        if (!admissionReport_->admitted) {
            throw std::runtime_error("Scheduler cannot initialize with a non-admitted interaction report");
        }

        for (const auto& conflict : admissionReport_->conflicts) {
            if (conflict.mode != ConflictResolutionMode::DeterministicPriority) {
                throw std::runtime_error(
                    "Scheduler only supports deterministic-priority conflict execution mode for now; variable='" +
                    conflict.variableName + "'");
            }
            if (conflict.selectedWriter.empty()) {
                throw std::runtime_error("Scheduler conflict arbitration selected empty owner");
            }
        }
        return;
    }

    std::unordered_map<std::string, std::string> ownership;

    for (const auto& subsystem : subsystems_) {
        for (const auto& variableName : subsystem->declaredWriteSet()) {
            const auto [it, inserted] = ownership.emplace(variableName, subsystem->name());
            if (!inserted && it->second != subsystem->name()) {
                throw std::runtime_error(
                    "Variable ownership conflict for '" + variableName + "' between subsystems '" +
                    it->second + "' and '" + subsystem->name() + "'");
            }
        }
    }
}

std::map<std::string, std::string, std::less<>> Scheduler::writeOwnershipByVariable() const {
    std::map<std::string, std::string, std::less<>> ownership;

    if (admissionReport_) {
        for (const auto& subsystem : orderedSubsystems()) {
            for (const auto& variableName : effectiveWriteSetFor(subsystem)) {
                ownership.insert_or_assign(variableName, subsystem->name());
            }
        }
        return ownership;
    }

    for (const auto& subsystem : subsystems_) {
        for (const auto& variableName : subsystem->declaredWriteSet()) {
            ownership.insert_or_assign(variableName, subsystem->name());
        }
    }
    return ownership;
}

void Scheduler::initialize(StateStore& stateStore, const ModelProfile& profile) {
    validateWriteOwnership();

    observedDataFlow_.clear();

    for (const auto& subsystem : orderedSubsystems()) {
        attachObserverForSubsystem(stateStore, subsystem);
        const auto writeSet = effectiveWriteSetFor(subsystem);
        StateStore::WriteSession session(
            stateStore,
            subsystem->name(),
            std::vector<std::string>(writeSet.begin(), writeSet.end()));
        subsystem->initialize(stateStore, session, profile);
        stateStore.clearAccessObserver();
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
    if (guardrailPolicy.multiRateMicroStepCount == 0) {
        throw std::invalid_argument("Scheduler multiRateMicroStepCount must be greater than zero");
    }
    if (guardrailPolicy.minAdaptiveSubIterations == 0 || guardrailPolicy.maxAdaptiveSubIterations == 0) {
        throw std::invalid_argument("Scheduler adaptive sub-iterations must be greater than zero");
    }
    if (guardrailPolicy.minAdaptiveSubIterations > guardrailPolicy.maxAdaptiveSubIterations) {
        throw std::invalid_argument("Scheduler adaptive sub-iteration range is invalid");
    }
    if (guardrailPolicy.dampingFactor <= 0.0f || guardrailPolicy.dampingFactor > 1.0f) {
        throw std::invalid_argument("Scheduler dampingFactor must be in (0, 1]");
    }
    if (guardrailPolicy.divergenceSoftLimit > guardrailPolicy.divergenceHardLimit) {
        throw std::invalid_argument("Scheduler divergence limits are invalid");
    }

    StepDiagnostics diagnostics;
    diagnostics.reproducibilityClass = classifyReproducibility(profile);
    diagnostics.executionPolicyMode = executionPolicyMode_;
    diagnostics.orderingLog.push_back("pipeline:begin_step_contracts");

    const auto ordered = orderedSubsystems();

    for (const auto& subsystem : ordered) {
        subsystem->preStep(profile, stepIndex);
    }

    // Only capture pre-step values when actually needed:
    // - MultiRateC uses it for drift reference / damping / fallback restore
    // - boundedIncrementEnabled uses it in the constraint pass
    // Skipping this copy for UniformA/PhasedB without boundedIncrement cuts a full
    // O(fields * cells) allocation and memcpy per step.
    const bool needsPreStepCapture =
        (temporalPolicy == TemporalPolicy::MultiRateC) ||
        guardrailPolicy.boundedIncrementEnabled;

    std::unordered_map<std::string, std::vector<float>> preStepValues;
    if (needsPreStepCapture) {
        for (const auto& variableName : stateStore.variableNames()) {
            const auto& field = stateStore.scalarField(variableName);
            const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
            preStepValues.emplace(variableName, std::vector<float>(field.begin(), field.begin() + static_cast<std::ptrdiff_t>(logicalCount)));
        }
    }

    diagnostics.orderingLog.push_back("pipeline:subsystem_updates");

    auto runUniformPass = [&]() {
        const bool canRunParallel =
            executionPolicyMode_ != ExecutionPolicyMode::StrictDeterministic &&
            temporalPolicy == TemporalPolicy::UniformA &&
            admissionReport_.has_value();

        if (!canRunParallel) {
            for (const auto& subsystem : ordered) {
                diagnostics.orderingLog.push_back("update:" + subsystem->name());
                attachObserverForSubsystem(stateStore, subsystem);
                const auto writeSet = effectiveWriteSetFor(subsystem);
                StateStore::WriteSession session(
                    stateStore,
                    subsystem->name(),
                    std::vector<std::string>(writeSet.begin(), writeSet.end()));
                subsystem->step(stateStore, session, profile, stepIndex);
                stateStore.clearAccessObserver();
            }
            return;
        }

        const auto batches = buildExecutionBatches(*admissionReport_, ordered);
        if (batches.empty()) {
            diagnostics.stabilityAlerts.push_back("parallel_execution_fallback:dependency_cycle_detected");
            for (const auto& subsystem : ordered) {
                diagnostics.orderingLog.push_back("update:" + subsystem->name());
                attachObserverForSubsystem(stateStore, subsystem);
                const auto writeSet = effectiveWriteSetFor(subsystem);
                StateStore::WriteSession session(
                    stateStore,
                    subsystem->name(),
                    std::vector<std::string>(writeSet.begin(), writeSet.end()));
                subsystem->step(stateStore, session, profile, stepIndex);
                stateStore.clearAccessObserver();
            }
            return;
        }

        std::map<std::string, std::shared_ptr<ISubsystem>, std::less<>> byName;
        for (const auto& subsystem : ordered) {
            byName.emplace(subsystem->name(), subsystem);
        }

        StateStoreSnapshot batchBaseline = stateStore.createSnapshot(0, 0, "parallel_execution_baseline");

        for (const auto& batch : batches) {
            diagnostics.parallelBatchesExecuted += 1;
            diagnostics.orderingLog.push_back("parallel_batch:size=" + std::to_string(batch.size()));

            std::vector<std::future<ParallelWriteBundle>> futures;
            futures.reserve(batch.size());

            for (const auto& subsystemName : batch) {
                const auto subsystemIt = byName.find(subsystemName);
                if (subsystemIt == byName.end()) {
                    throw std::runtime_error("Parallel execution subsystem lookup failed: " + subsystemName);
                }

                const auto& subsystem = subsystemIt->second;
                const auto writeSet = effectiveWriteSetFor(subsystem);
                const auto baselineCopy = std::make_shared<StateStoreSnapshot>(batchBaseline);

                diagnostics.parallelTasksDispatched += 1;
                futures.push_back(std::async(std::launch::async, [&, subsystem, writeSet, baselineCopy]() {
                    StateStore localState(
                        stateStore.grid(),
                        stateStore.boundaryMode(),
                        stateStore.topologyBackend(),
                        stateStore.memoryLayoutPolicy());
                    localState.loadSnapshot(*baselineCopy, 0, 0);

                    StateStore::WriteSession localWriteSession(
                        localState,
                        subsystem->name(),
                        std::vector<std::string>(writeSet.begin(), writeSet.end()));
                    subsystem->step(localState, localWriteSession, profile, stepIndex);

                    ParallelWriteBundle bundle;
                    bundle.subsystemName = subsystem->name();
                    const auto declaredReads = subsystem->declaredReadSet();
                    bundle.observedReads.insert(
                        declaredReads.begin(),
                        declaredReads.end());
                    bundle.observedWrites.insert(
                        writeSet.begin(),
                        writeSet.end());

                    for (const auto& variableName : writeSet) {
                        const auto& field = localState.scalarField(variableName);
                        const auto logicalCount = static_cast<std::size_t>(localState.logicalCellCount(variableName));
                        bundle.scalarWrites.emplace(
                            variableName,
                            std::vector<float>(field.begin(), field.begin() + static_cast<std::ptrdiff_t>(logicalCount)));
                    }

                    return bundle;
                }));
            }

            std::vector<ParallelWriteBundle> completedBundles;
            if (executionPolicyMode_ == ExecutionPolicyMode::ThroughputPriority) {
                std::vector<bool> collected(futures.size(), false);
                std::size_t remaining = futures.size();
                while (remaining > 0) {
                    bool progress = false;
                    for (std::size_t i = 0; i < futures.size(); ++i) {
                        if (collected[i]) {
                            continue;
                        }
                        if (futures[i].wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                            completedBundles.push_back(futures[i].get());
                            collected[i] = true;
                            remaining -= 1;
                            progress = true;
                        }
                    }
                    if (!progress) {
                        std::this_thread::yield();
                    }
                }
            } else {
                for (auto& future : futures) {
                    completedBundles.push_back(future.get());
                }
                std::map<std::string, std::size_t, std::less<>> batchOrder;
                for (std::size_t i = 0; i < batch.size(); ++i) {
                    batchOrder.emplace(batch[i], i);
                }
                std::sort(completedBundles.begin(), completedBundles.end(), [&](const auto& left, const auto& right) {
                    return batchOrder.at(left.subsystemName) < batchOrder.at(right.subsystemName);
                });
            }

            for (const auto& bundle : completedBundles) {
                auto& observation = observedDataFlow_[bundle.subsystemName];
                observation.reads.insert(bundle.observedReads.begin(), bundle.observedReads.end());
                observation.writes.insert(bundle.observedWrites.begin(), bundle.observedWrites.end());

                std::vector<std::string> writeVariables;
                writeVariables.reserve(bundle.scalarWrites.size());
                for (const auto& [variableName, values] : bundle.scalarWrites) {
                    (void)values;
                    writeVariables.push_back(variableName);
                }

                StateStore::WriteSession commitSession(stateStore, bundle.subsystemName, writeVariables);
                for (const auto& [variableName, values] : bundle.scalarWrites) {
                    for (std::size_t i = 0; i < values.size(); ++i) {
                        commitSession.setScalar(variableName, stateStore.cellFromIndex(static_cast<std::uint64_t>(i)), values[i]);
                    }
                }

                diagnostics.orderingLog.push_back("parallel_commit:" + bundle.subsystemName);
            }

            batchBaseline = stateStore.createSnapshot(0, 0, "parallel_execution_batch_commit");
        }

        stateStore.clearAccessObserver();
    };

    auto runPhasedPass = [&]() {
        for (std::uint32_t phase = 0; phase < 2; ++phase) {
            for (const auto& subsystem : ordered) {
                const auto tierIt = profile.subsystemTiers.find(subsystem->name());
                const ModelTier tier = tierIt == profile.subsystemTiers.end() ? ModelTier::A : tierIt->second;

                if (!matchesPhase(tier, phase)) {
                    continue;
                }

                diagnostics.orderingLog.push_back("phase" + std::to_string(phase) + ":update:" + subsystem->name());
                attachObserverForSubsystem(stateStore, subsystem);
                const auto writeSet = effectiveWriteSetFor(subsystem);
                StateStore::WriteSession session(
                    stateStore,
                    subsystem->name(),
                    std::vector<std::string>(writeSet.begin(), writeSet.end()));
                subsystem->step(stateStore, session, profile, stepIndex);
                stateStore.clearAccessObserver();
            }
        }
    };

    const double preWaterMass = stateStore.hasField("surface_water_w") ? computeTotal(stateStore, "surface_water_w") : 0.0;
    const double preResourceMass = stateStore.hasField("resource_stock_r") ? computeTotal(stateStore, "resource_stock_r") : 0.0;

    if (temporalPolicy == TemporalPolicy::UniformA) {
        runUniformPass();
    } else if (temporalPolicy == TemporalPolicy::PhasedB) {
        runPhasedPass();
    } else {
        FieldSnapshot macroReference = preStepValues;
        double previousDrift = std::numeric_limits<double>::infinity();
        bool fallbackUsed = false;

        diagnostics.stability.microStepsExecuted = guardrailPolicy.multiRateMicroStepCount;
        for (std::uint32_t microStep = 0; microStep < guardrailPolicy.multiRateMicroStepCount; ++microStep) {
            const double predictor = std::isfinite(previousDrift) ? previousDrift : 0.0;
            const std::uint32_t subIterations = predictor > static_cast<double>(guardrailPolicy.stiffnessDriftThreshold)
                ? guardrailPolicy.maxAdaptiveSubIterations
                : guardrailPolicy.minAdaptiveSubIterations;
            diagnostics.stability.adaptiveSubIterations += subIterations;

            for (std::uint32_t iteration = 0; iteration < subIterations; ++iteration) {
                for (const auto& subsystem : ordered) {
                    diagnostics.orderingLog.push_back(
                        "micro" + std::to_string(microStep) +
                        ":iter" + std::to_string(iteration) +
                        ":update:" + subsystem->name());
                    attachObserverForSubsystem(stateStore, subsystem);
                    const auto writeSet = effectiveWriteSetFor(subsystem);
                    StateStore::WriteSession session(
                        stateStore,
                        subsystem->name(),
                        std::vector<std::string>(writeSet.begin(), writeSet.end()));
                    subsystem->step(stateStore, session, profile, stepIndex);
                    stateStore.clearAccessObserver();
                }
            }

            const double currentDrift = computeDriftMetric(stateStore, macroReference);
            diagnostics.stability.driftMetric = std::max(diagnostics.stability.driftMetric, currentDrift);

            if (std::isfinite(previousDrift) && previousDrift > 0.0) {
                const double amplification = currentDrift / previousDrift;
                diagnostics.stability.amplificationIndicator = std::max(diagnostics.stability.amplificationIndicator, amplification);
            }

            if (currentDrift > static_cast<double>(guardrailPolicy.divergenceSoftLimit)) {
                diagnostics.stability.finalEscalationAction = EscalationAction::Damping;
                diagnostics.stability.dampingApplications += 1;
                diagnostics.stabilityAlerts.push_back(
                    "stability_damping:micro=" + std::to_string(microStep) +
                    ":drift=" + std::to_string(currentDrift));
                applyDampingFromReference(stateStore, macroReference, guardrailPolicy.dampingFactor);
            }

            const double postControlDrift = computeDriftMetric(stateStore, macroReference);
            diagnostics.stability.driftMetric = std::max(diagnostics.stability.driftMetric, postControlDrift);

            if (postControlDrift > static_cast<double>(guardrailPolicy.divergenceHardLimit)) {
                if (guardrailPolicy.enableControlledFallback && !fallbackUsed) {
                    diagnostics.stability.finalEscalationAction = EscalationAction::ControlledFallback;
                    diagnostics.stability.fallbackApplications += 1;
                    diagnostics.stabilityAlerts.push_back(
                        "stability_fallback:micro=" + std::to_string(microStep) +
                        ":drift=" + std::to_string(postControlDrift));
                    restoreFieldSnapshot(stateStore, macroReference, "stability_fallback_restore");
                    runPhasedPass();
                    fallbackUsed = true;
                } else {
                    diagnostics.stability.finalEscalationAction = EscalationAction::SafeAbort;
                    diagnostics.stabilityAlerts.push_back(
                        "stability_safe_abort:micro=" + std::to_string(microStep) +
                        ":drift=" + std::to_string(postControlDrift));
                    throw std::runtime_error("Scheduler safe abort triggered by divergence in multi-rate execution");
                }
            }

            previousDrift = postControlDrift;
            macroReference = captureFieldSnapshot(stateStore);
        }
    }

    diagnostics.orderingLog.push_back("pipeline:constraint_pass");
    stateStore.clearAccessObserver();

    // Skip full constraint writes when no guardrails are active — the NaN scan below
    // still runs as a safety net. This is the hot path for UniformA/PhasedB.
    const bool needsConstraintPass = guardrailPolicy.clampEnabled ||
                                     guardrailPolicy.boundedIncrementEnabled;

    if (needsConstraintPass) {
        StateStore::WriteSession constraintSession(stateStore, "constraint_pass", stateStore.variableNames());
        for (const auto& variableName : stateStore.variableNames()) {
            const auto& postValues = stateStore.scalarField(variableName);
            const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));

            // Only look up preValues when boundedIncrement actually needs them.
            const std::vector<float>* preValuesPtr = nullptr;
            if (guardrailPolicy.boundedIncrementEnabled) {
                const auto it = preStepValues.find(variableName);
                if (it != preStepValues.end()) {
                    preValuesPtr = &it->second;
                }
            }

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

                if (guardrailPolicy.boundedIncrementEnabled && preValuesPtr != nullptr) {
                    const float previous = (*preValuesPtr)[i];
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
    } else {
        // Fast-path: still scan for NaN/Inf to catch numerical instabilities early.
        for (const auto& variableName : stateStore.variableNames()) {
            const auto& postValues = stateStore.scalarField(variableName);
            const std::size_t logicalCount = static_cast<std::size_t>(stateStore.logicalCellCount(variableName));
            for (std::size_t i = 0; i < logicalCount; ++i) {
                if (!std::isfinite(postValues[i])) {
                    std::ostringstream error;
                    error << "Numerical fail-fast: non-finite value for variable='" << variableName << "' index=" << i;
                    throw std::runtime_error(error.str());
                }
            }
        }
    }

    diagnostics.orderingLog.push_back("pipeline:commit_step");
    for (const auto& subsystem : ordered) {
        subsystem->postStep(profile, stepIndex);
    }

    const double postWaterMass = stateStore.hasField("surface_water_w") ? computeTotal(stateStore, "surface_water_w") : preWaterMass;
    const double postResourceMass = stateStore.hasField("resource_stock_r") ? computeTotal(stateStore, "resource_stock_r") : preResourceMass;
    diagnostics.stability.conservationResidualWater = postWaterMass - preWaterMass;
    diagnostics.stability.conservationResidualResources = postResourceMass - preResourceMass;

    return diagnostics;
}

std::vector<std::string> Scheduler::activeSubsystemNames() const {
    const auto ordered = orderedSubsystems();
    std::vector<std::string> names;
    names.reserve(ordered.size());
    for (const auto& subsystem : ordered) {
        names.push_back(subsystem->name());
    }
    return names;
}

void Scheduler::validateObservedDataFlow() const {
    if (!admissionReport_) {
        return;
    }
    InteractionCoordinator::validateObservedDataFlow(*admissionReport_, observedDataFlow_);
}

} // namespace ws
