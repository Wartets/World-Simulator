#include "ws/core/initialization_binding.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

std::filesystem::path resolveModelsRoot() {
    const std::filesystem::path direct = "models";
    if (std::filesystem::exists(direct) && std::filesystem::is_directory(direct)) {
        return direct;
    }

    const std::filesystem::path parent = std::filesystem::path("..") / "models";
    if (std::filesystem::exists(parent) && std::filesystem::is_directory(parent)) {
        return parent;
    }

    return {};
}

void verifyModelCatalogAndExecutionSpecConsistency(const std::filesystem::path& modelPath) {
    ws::initialization::ModelVariableCatalog catalog;
    std::string catalogMessage;
    const bool catalogOk = ws::initialization::loadModelVariableCatalog(modelPath, catalog, catalogMessage);
    assert(catalogOk);
    assert(!catalog.variables.empty());

    ws::ModelExecutionSpec executionSpec;
    std::string executionMessage;
    const bool executionOk = ws::initialization::loadModelExecutionSpec(modelPath, executionSpec, executionMessage);
    assert(executionOk);
    assert(!executionSpec.cellScalarVariableIds.empty());

    std::set<std::string> variableIds(
        executionSpec.cellScalarVariableIds.begin(),
        executionSpec.cellScalarVariableIds.end());

    for (const auto& [semanticKey, variableId] : executionSpec.semanticFieldAliases) {
        assert(!semanticKey.empty());
        assert(!variableId.empty());
        assert(variableIds.contains(variableId));
    }

    const auto stateIds = catalog.cellStateVariableIds();
    assert(!stateIds.empty());

    ws::initialization::InitializationRequest conwayRequest;
    conwayRequest.type = ws::InitialConditionType::Conway;
    const auto conwayPlan = ws::initialization::buildBindingPlan(catalog, conwayRequest);

    ws::initialization::InitializationRequest wavesRequest;
    wavesRequest.type = ws::InitialConditionType::Waves;
    const auto wavesPlan = ws::initialization::buildBindingPlan(catalog, wavesRequest);

    const bool hasConwayTarget = !conwayPlan.decisions.empty() && conwayPlan.decisions.front().resolved;
    const bool hasWaveTarget = !wavesPlan.decisions.empty() && wavesPlan.decisions.front().resolved;

    // Metadata-driven mode discovery: models may support none, one, or multiple init modes.
    // The strict binding contract still requires deterministic outcomes.
    if (!hasConwayTarget) {
        assert(conwayPlan.hasBlockingIssues());
    }
    if (!hasWaveTarget) {
        assert(wavesPlan.hasBlockingIssues());
    }
}

} // namespace

int main() {
    const std::filesystem::path modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        std::cout << "Skipping integration_multi_model test: models root not found.\n";
        return 0;
    }

    std::size_t verified = 0;
    for (const auto& entry : std::filesystem::directory_iterator(modelsRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (entry.path().extension() != ".simmodel") {
            continue;
        }

        verifyModelCatalogAndExecutionSpecConsistency(entry.path());
        ++verified;
    }

    assert(verified > 0);
    std::cout << "integration_multi_model verified models=" << verified << "\n";
    return 0;
}
