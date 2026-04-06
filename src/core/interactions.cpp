#include "ws/core/interactions.hpp"

#include "ws/core/determinism.hpp"
#include "ws/core/scheduler.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ws {

namespace {

// Ordered string set for dependency tracking.
using OrderedSet = std::set<std::string, std::less<>>;

// Converts DependencyEdgeKind enum to string.
std::string toString(const DependencyEdgeKind kind) {
    switch (kind) {
        case DependencyEdgeKind::Data: return "data";
        case DependencyEdgeKind::TemporalRate: return "temporal_rate";
    }
    return "unknown";
}

// Converts ConflictResolutionMode enum to string.
std::string toString(const ConflictResolutionMode mode) {
    switch (mode) {
        case ConflictResolutionMode::DeterministicPriority: return "deterministic_priority";
        case ConflictResolutionMode::MergeSum: return "merge_sum";
        case ConflictResolutionMode::MergeAverage: return "merge_average";
    }
    return "unknown";
}

// Converts AdmissionIssue severity to string.
std::string toString(const AdmissionIssue::Severity severity) {
    switch (severity) {
        case AdmissionIssue::Severity::Info: return "info";
        case AdmissionIssue::Severity::Warning: return "warning";
        case AdmissionIssue::Severity::Error: return "error";
    }
    return "unknown";
}

std::uint64_t tierPriority(const ModelTier tier) {
    switch (tier) {
        case ModelTier::C: return 2;
        case ModelTier::B: return 1;
        case ModelTier::A: return 0;
    }
    return 0;
}

std::vector<std::string> sortedVector(const OrderedSet& values) {
    return std::vector<std::string>(values.begin(), values.end());
}

std::pair<ReproducibilityClass, double> classifyReproducibility(const std::map<std::string, ModelTier, std::less<>>& tierBySubsystem) {
    std::size_t cTierCount = 0;
    for (const auto& [subsystemName, tier] : tierBySubsystem) {
        if (subsystemName == "temporal") {
            continue;
        }
        if (tier == ModelTier::C) {
            cTierCount += 1;
        }
    }

    if (cTierCount == 0) {
        return {ReproducibilityClass::Strict, 0.0};
    }
    if (cTierCount <= 3) {
        return {ReproducibilityClass::BoundedDivergence, 1e-4};
    }
    return {ReproducibilityClass::Exploratory, 5e-3};
}

OrderedSet normalizeConservedVariables(const std::vector<std::string>& conservedVariables) {
    OrderedSet normalized;
    for (const auto& variableName : conservedVariables) {
        if (variableName.empty()) {
            continue;
        }
        normalized.insert(variableName);
    }
    return normalized;
}

OrderedSet externalSourcesFromAssumptions(const std::set<std::string, std::less<>>& compatibilityAssumptions) {
    OrderedSet externalSources;
    constexpr std::string_view externalSourcePrefix = "external_source:";
    constexpr std::string_view externalSourceAltPrefix = "external_source=";

    for (const auto& assumption : compatibilityAssumptions) {
        std::string_view view(assumption);
        if (view.rfind(externalSourcePrefix, 0) == 0) {
            const std::string variableName(view.substr(externalSourcePrefix.size()));
            if (!variableName.empty()) {
                externalSources.insert(variableName);
            }
            continue;
        }

        if (view.rfind(externalSourceAltPrefix, 0) == 0) {
            const std::string variableName(view.substr(externalSourceAltPrefix.size()));
            if (!variableName.empty()) {
                externalSources.insert(variableName);
            }
        }
    }

    return externalSources;
}

} // namespace

std::string AdmissionReport::diagnosticsText() const {
    std::ostringstream stream;
    stream << "admitted=" << (admitted ? "true" : "false") << ';';
    stream << "fingerprint=" << fingerprint << ';';
    stream << "repro_class=" << toString(reproducibilityClass) << ';';
    stream << "repro_tolerance=" << reproducibilityTolerance << ';';

    for (const auto& issue : issues) {
        stream << '[' << toString(issue.severity) << ':' << issue.code << "] " << issue.message << '\n';
    }

    if (!conflicts.empty()) {
        stream << "conflicts=" << conflicts.size() << '\n';
    }

    return stream.str();
}

AdmissionReport InteractionCoordinator::buildAdmissionReport(
    const ModelProfile& profile,
    const TemporalPolicy temporalPolicy,
    const std::vector<std::shared_ptr<ISubsystem>>& subsystems) const {
    AdmissionReport report;

    std::map<std::string, std::shared_ptr<ISubsystem>, std::less<>> subsystemByName;
    for (const auto& subsystem : subsystems) {
        if (!subsystem) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "SUBSYSTEM_NULL",
                "Encountered null subsystem registration"});
            continue;
        }

        const std::string subsystemName = subsystem->name();
        if (subsystemName.empty()) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "SUBSYSTEM_NAME_EMPTY",
                "Encountered subsystem with empty name"});
            continue;
        }

        if (subsystemByName.contains(subsystemName)) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "SUBSYSTEM_DUPLICATE",
                "Duplicate subsystem registration: " + subsystemName});
            continue;
        }

        subsystemByName.emplace(subsystemName, subsystem);
    }

    std::map<std::string, ModelTier, std::less<>> tierBySubsystem;
    for (const auto& [name, subsystem] : subsystemByName) {
        const auto tierIt = profile.subsystemTiers.find(name);
        if (tierIt == profile.subsystemTiers.end()) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "PROFILE_TIER_MISSING",
                "Profile tier missing for active subsystem: " + name});
            continue;
        }

        tierBySubsystem.emplace(name, tierIt->second);
        report.nodes.push_back(DependencyNode{name, tierIt->second});

        SubsystemContractReport contract;
        contract.subsystemName = name;
        const auto declaredReads = subsystem->declaredReadSet();
        const auto declaredWrites = subsystem->declaredWriteSet();
        contract.declaredReads = OrderedSet(declaredReads.begin(), declaredReads.end());
        contract.declaredWrites = OrderedSet(declaredWrites.begin(), declaredWrites.end());
        report.subsystemContracts.push_back(std::move(contract));
    }

    const auto temporalTierIt = profile.subsystemTiers.find("temporal");
    if (temporalTierIt != profile.subsystemTiers.end()) {
        if (temporalTierIt->second == ModelTier::A && temporalPolicy != TemporalPolicy::UniformA) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "TEMPORAL_POLICY_MISMATCH",
                "Temporal subsystem tier A requires UniformA execution policy"});
        }
        if (temporalTierIt->second == ModelTier::B && temporalPolicy != TemporalPolicy::PhasedB) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "TEMPORAL_POLICY_MISMATCH",
                "Temporal subsystem tier B requires PhasedB execution policy"});
        }
        if (temporalTierIt->second == ModelTier::C && temporalPolicy != TemporalPolicy::MultiRateC) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "TEMPORAL_POLICY_MISMATCH",
                "Temporal subsystem tier C requires MultiRateC execution policy"});
        }
    }

    bool hasAnyCTier = false;
    for (const auto& [subsystemName, tier] : tierBySubsystem) {
        if (subsystemName != "temporal" && tier == ModelTier::C) {
            hasAnyCTier = true;
            break;
        }
    }

    if (hasAnyCTier) {
        if (!profile.subsystemTiers.contains("temporal") || profile.subsystemTiers.at("temporal") != ModelTier::C) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "TEMPORAL_TIER_REQUIRED",
                "A C-tier subsystem selection requires the temporal subsystem to be tier C"});
        }
        if (temporalPolicy != TemporalPolicy::MultiRateC) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "TEMPORAL_POLICY_REQUIRED",
                "A C-tier subsystem selection requires the MultiRateC execution policy"});
        }
    }

    std::map<std::string, OrderedSet, std::less<>> writersByVariable;
    std::map<std::string, OrderedSet, std::less<>> readersByVariable;

    for (const auto& [name, subsystem] : subsystemByName) {
        for (const auto& variableName : subsystem->declaredWriteSet()) {
            writersByVariable[variableName].insert(name);
        }
        for (const auto& variableName : subsystem->declaredReadSet()) {
            readersByVariable[variableName].insert(name);
        }
    }

    const OrderedSet externalSourceVariables = externalSourcesFromAssumptions(profile.compatibilityAssumptions);
    const bool strictReadSources = profile.compatibilityAssumptions.contains("strict_read_sources");

    for (const auto& [variableName, readers] : readersByVariable) {
        if (!writersByVariable.contains(variableName)) {
            if (externalSourceVariables.contains(variableName)) {
                report.issues.push_back(AdmissionIssue{
                    AdmissionIssue::Severity::Info,
                    "READ_SOURCE_EXTERNAL_DECLARED",
                    "Variable is read from declared external source: " + variableName});
            } else if (strictReadSources) {
                report.issues.push_back(AdmissionIssue{
                    AdmissionIssue::Severity::Error,
                    "READ_SOURCE_MISSING",
                    "No writer or external source for variable: " + variableName});
            } else {
                report.issues.push_back(AdmissionIssue{
                    AdmissionIssue::Severity::Warning,
                    "READ_SOURCE_EXTERNAL_ASSUMED",
                    "Variable read without internal writer; treated as external source: " + variableName});
            }
        }

        if (writersByVariable.contains(variableName)) {
            for (const auto& writer : writersByVariable.at(variableName)) {
                for (const auto& reader : readers) {
                    if (writer == reader) {
                        continue;
                    }
                    report.edges.push_back(DependencyEdge{writer, reader, variableName, DependencyEdgeKind::Data});
                }
            }
        }
    }

    if (temporalPolicy == TemporalPolicy::PhasedB) {
        for (const auto& source : report.nodes) {
            if (source.tier != ModelTier::A) {
                continue;
            }

            for (const auto& target : report.nodes) {
                if (target.subsystemName == source.subsystemName || target.tier == ModelTier::A) {
                    continue;
                }
                report.edges.push_back(DependencyEdge{
                    source.subsystemName,
                    target.subsystemName,
                    "temporal_phase_dependency",
                    DependencyEdgeKind::TemporalRate});
            }
        }
    }

    if (temporalPolicy == TemporalPolicy::MultiRateC) {
        std::vector<std::string> activeStrongCoupled;
        activeStrongCoupled.reserve(tierBySubsystem.size());
        for (const auto& [name, tier] : tierBySubsystem) {
            if (name == "temporal") {
                continue;
            }
            if (tier == ModelTier::C) {
                activeStrongCoupled.push_back(name);
            }
        }

        std::sort(activeStrongCoupled.begin(), activeStrongCoupled.end());
        for (std::size_t i = 0; i < activeStrongCoupled.size(); ++i) {
            for (std::size_t j = i + 1; j < activeStrongCoupled.size(); ++j) {
                report.edges.push_back(DependencyEdge{
                    activeStrongCoupled[i],
                    activeStrongCoupled[j],
                    "strong_coupling_channel",
                    DependencyEdgeKind::TemporalRate});
                report.edges.push_back(DependencyEdge{
                    activeStrongCoupled[j],
                    activeStrongCoupled[i],
                    "strong_coupling_channel",
                    DependencyEdgeKind::TemporalRate});
            }
        }
    }

    std::sort(report.edges.begin(), report.edges.end(), [](const auto& left, const auto& right) {
        if (left.fromSubsystem != right.fromSubsystem) {
            return left.fromSubsystem < right.fromSubsystem;
        }
        if (left.toSubsystem != right.toSubsystem) {
            return left.toSubsystem < right.toSubsystem;
        }
        if (left.variableName != right.variableName) {
            return left.variableName < right.variableName;
        }
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    });

    for (const auto& [variableName, writers] : writersByVariable) {
        if (writers.size() <= 1) {
            continue;
        }

        ConflictResolution conflict;
        conflict.variableName = variableName;
        conflict.mode = ConflictResolutionMode::DeterministicPriority;

        conflict.writersInPriorityOrder = sortedVector(writers);
        std::stable_sort(
            conflict.writersInPriorityOrder.begin(),
            conflict.writersInPriorityOrder.end(),
            [&](const std::string& left, const std::string& right) {
                const auto leftTier = tierBySubsystem.contains(left) ? tierBySubsystem.at(left) : ModelTier::A;
                const auto rightTier = tierBySubsystem.contains(right) ? tierBySubsystem.at(right) : ModelTier::A;
                const std::uint64_t leftPriority = tierPriority(leftTier);
                const std::uint64_t rightPriority = tierPriority(rightTier);
                if (leftPriority != rightPriority) {
                    return leftPriority > rightPriority;
                }
                return left < right;
            });

        conflict.selectedWriter = conflict.writersInPriorityOrder.front();
        report.conflicts.push_back(conflict);

        report.issues.push_back(AdmissionIssue{
            AdmissionIssue::Severity::Warning,
            "WRITE_CONFLICT_ARBITRATED",
            "Variable '" + variableName + "' has competing writers; selected writer='" + conflict.selectedWriter +
                "' mode='" + toString(conflict.mode) + "'"});
    }

    const OrderedSet conservedVariables = normalizeConservedVariables(profile.conservedVariables);

    for (const auto& variableName : conservedVariables) {
        const bool variableIsActive = writersByVariable.contains(variableName) || readersByVariable.contains(variableName);
        if (!variableIsActive) {
            continue;
        }

        const auto writerIt = writersByVariable.find(variableName);
        if (writerIt == writersByVariable.end()) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "CONSERVATION_OWNER_MISSING",
                "Conservation-protected variable has no owner: " + variableName});
            continue;
        }

        if (writerIt->second.size() > 1) {
            report.issues.push_back(AdmissionIssue{
                AdmissionIssue::Severity::Error,
                "CONSERVATION_OWNER_CONFLICT",
                "Conservation-protected variable has multiple owners: " + variableName});
        }
    }

    std::map<std::string, OrderedSet, std::less<>> adjacency;
    std::map<std::string, std::uint64_t, std::less<>> indegree;
    for (const auto& node : report.nodes) {
        adjacency[node.subsystemName] = {};
        indegree[node.subsystemName] = 0;
    }

    for (const auto& edge : report.edges) {
        if (!adjacency.contains(edge.fromSubsystem) || !indegree.contains(edge.toSubsystem)) {
            continue;
        }
        if (adjacency[edge.fromSubsystem].insert(edge.toSubsystem).second) {
            indegree[edge.toSubsystem] += 1;
        }
    }

    std::priority_queue<std::string, std::vector<std::string>, std::greater<>> ready;
    for (const auto& [name, degree] : indegree) {
        if (degree == 0) {
            ready.push(name);
        }
    }

    while (!ready.empty()) {
        const std::string current = ready.top();
        ready.pop();
        report.deterministicOrder.push_back(current);

        for (const auto& next : adjacency[current]) {
            auto& degree = indegree[next];
            degree -= 1;
            if (degree == 0) {
                ready.push(next);
            }
        }
    }

    if (report.deterministicOrder.size() != report.nodes.size()) {
        OrderedSet unresolved;
        for (const auto& node : report.nodes) {
            if (std::find(report.deterministicOrder.begin(), report.deterministicOrder.end(), node.subsystemName) == report.deterministicOrder.end()) {
                unresolved.insert(node.subsystemName);
            }
        }

        for (const auto& name : unresolved) {
            report.deterministicOrder.push_back(name);
        }

        report.issues.push_back(AdmissionIssue{
            AdmissionIssue::Severity::Warning,
            "DEPENDENCY_CYCLE_DETECTED",
            "Dependency cycle detected; deterministic lexical fallback order appended"});
    }

    std::sort(report.nodes.begin(), report.nodes.end(), [](const auto& left, const auto& right) {
        return left.subsystemName < right.subsystemName;
    });
    std::sort(report.subsystemContracts.begin(), report.subsystemContracts.end(), [](const auto& left, const auto& right) {
        return left.subsystemName < right.subsystemName;
    });

    std::ostringstream serialization;
    const auto [reproClass, reproTolerance] = classifyReproducibility(tierBySubsystem);
    report.reproducibilityClass = reproClass;
    report.reproducibilityTolerance = reproTolerance;

    serialization << "repro_class|" << toString(report.reproducibilityClass)
                  << "|tolerance=" << report.reproducibilityTolerance << '\n';
    for (const auto& node : report.nodes) {
        serialization << "node|" << node.subsystemName << "|tier=" << toString(node.tier) << '\n';
    }
    for (const auto& edge : report.edges) {
        serialization << "edge|" << edge.fromSubsystem << "->" << edge.toSubsystem
                      << "|var=" << edge.variableName << "|kind=" << toString(edge.kind) << '\n';
    }
    for (const auto& conflict : report.conflicts) {
        serialization << "conflict|var=" << conflict.variableName << "|mode=" << toString(conflict.mode)
                      << "|selected=" << conflict.selectedWriter << "|writers=";
        for (const auto& writer : conflict.writersInPriorityOrder) {
            serialization << writer << ',';
        }
        serialization << '\n';
    }
    for (const auto& item : report.deterministicOrder) {
        serialization << "order|" << item << '\n';
    }

    report.serializedGraph = serialization.str();

    report.admitted = true;
    for (const auto& issue : report.issues) {
        if (issue.severity == AdmissionIssue::Severity::Error) {
            report.admitted = false;
            break;
        }
    }

    std::uint64_t fingerprint = DeterministicHash::offsetBasis;
    fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(report.serializedGraph));
    fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashPod(report.admitted));
    for (const auto& issue : report.issues) {
        fingerprint = DeterministicHash::combine(fingerprint, static_cast<std::uint64_t>(issue.severity));
        fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(issue.code));
        fingerprint = DeterministicHash::combine(fingerprint, DeterministicHash::hashString(issue.message));
    }
    report.fingerprint = fingerprint;

    return report;
}

void InteractionCoordinator::validateObservedDataFlow(
    const AdmissionReport& report,
    const std::map<std::string, AccessObservation, std::less<>>& observedBySubsystem) {
    if (!report.admitted) {
        throw std::runtime_error("Cannot validate observed data flow for a non-admitted interaction report");
    }

    for (const auto& contract : report.subsystemContracts) {
        if (!observedBySubsystem.contains(contract.subsystemName)) {
            continue;
        }

        const auto& observed = observedBySubsystem.at(contract.subsystemName);
        for (const auto& variableName : observed.reads) {
            if (!contract.declaredReads.contains(variableName)) {
                throw std::runtime_error(
                    "Data-flow read violation: subsystem='" + contract.subsystemName +
                    "' observed undeclared read variable='" + variableName + "'");
            }
        }

        for (const auto& variableName : observed.writes) {
            if (!contract.declaredWrites.contains(variableName)) {
                throw std::runtime_error(
                    "Data-flow write violation: subsystem='" + contract.subsystemName +
                    "' observed undeclared write variable='" + variableName + "'");
            }
        }
    }
}

} // namespace ws
