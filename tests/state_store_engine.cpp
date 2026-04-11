#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <memory>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::Minimal;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::Minimal;
    input.compatibilityAssumptions = {
        "state_store_test_assumption",
        "deterministic_state_store"
    };
    return input;
}

ws::RuntimeConfig baselineConfig(const std::uint64_t seed) {
    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{16, 16};
    config.boundaryMode = ws::BoundaryMode::Wrap;
    config.memoryLayoutPolicy = ws::MemoryLayoutPolicy{64, 32, 1};
    config.profileInput = baselineProfileInput();
    return config;
}

void verifySnapshotReplayDeterminism() {
    ws::Runtime baselineRuntime(baselineConfig(987654));
    baselineRuntime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    baselineRuntime.start();

    const ws::RuntimeCheckpoint checkpoint = baselineRuntime.createCheckpoint("pre_step_checkpoint");
    baselineRuntime.step();
    const ws::RuntimeSnapshot reference = baselineRuntime.snapshot();

    ws::Runtime replayRuntime(baselineConfig(987654));
    replayRuntime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    replayRuntime.start();
    replayRuntime.loadCheckpoint(checkpoint);
    replayRuntime.step();
    const ws::RuntimeSnapshot replay = replayRuntime.snapshot();

    assert(reference.runSignature.identityHash() == replay.runSignature.identityHash());
    assert(reference.stateHash == replay.stateHash);
    assert(reference.stateHeader.stepIndex == replay.stateHeader.stepIndex);
    assert(reference.payloadBytes == replay.payloadBytes);
}

void verifyBoundaryValidityAndOverlayContracts() {
    ws::StateStore stateStore(
        ws::GridSpec{4, 2},
        ws::BoundaryMode::Wrap,
        ws::GridTopologyBackend::Cartesian2D,
        ws::MemoryLayoutPolicy{64, 4, 1});

    stateStore.allocateScalarField(ws::VariableSpec{0, "probe"});

    ws::StateStore::WriteSession writer(stateStore, "contract_test", {"probe"});
    writer.setScalar("probe", ws::Cell{3, 0}, 7.0f);

    const auto wrappedSample = stateStore.trySampleScalar("probe", ws::CellSigned{-1, 0});
    assert(wrappedSample.has_value());
    assert(*wrappedSample == 7.0f);

    writer.invalidateScalar("probe", ws::Cell{3, 0});
    const auto invalidSample = stateStore.trySampleScalar("probe", ws::CellSigned{-1, 0});
    assert(!invalidSample.has_value());

    writer.setOverlayScalar("probe", ws::Cell{3, 0}, 9.0f);
    const auto overlaySample = stateStore.trySampleScalar("probe", ws::CellSigned{-1, 0});
    assert(overlaySample.has_value());
    assert(*overlaySample == 9.0f);

    writer.clearOverlayScalar("probe", ws::Cell{3, 0});
    const auto restoredBaseSample = stateStore.trySampleScalar("probe", ws::CellSigned{-1, 0});
    assert(restoredBaseSample.has_value());
    assert(*restoredBaseSample == 7.0f);

    const auto metadata = stateStore.fieldMetadata();
    assert(metadata.size() == 1);
    assert(metadata.front().logicalCellCount == 8);
    assert(metadata.front().paddedCellCount == 8);
    assert(metadata.front().alignmentBytes == 64);
}

void verifyDynamicLayoutScaffolding() {
    ws::StateStore stateStore(
        ws::GridSpec{3, 2},
        ws::BoundaryMode::Clamp,
        ws::GridTopologyBackend::Cartesian2D,
        ws::MemoryLayoutPolicy{64, 3, 1});

    const auto firstHandle = stateStore.addVariable(ws::VariableSpec{10, "alpha"});
    const auto secondHandle = stateStore.addVariable(ws::VariableSpec{11, "beta"});

    assert(firstHandle != ws::StateStore::InvalidHandle);
    assert(secondHandle != ws::StateStore::InvalidHandle);
    assert(firstHandle != secondHandle);

    const auto& layout = stateStore.getLayout();
    assert(layout.grid.width == 3);
    assert(layout.grid.height == 2);
    assert(layout.fields.size() == 2);
    assert(layout.fields[0].spec.name == "alpha");
    assert(layout.fields[1].spec.name == "beta");
    assert(layout.fields[0].valuesOffsetBytes == 0);
    assert(layout.fields[1].valuesOffsetBytes >= layout.fields[0].valuesOffsetBytes);
    assert(layout.valuesBufferBytes > 0);
    assert(layout.validityMaskBufferBytes > 0);
}

} // namespace

int main() {
    verifySnapshotReplayDeterminism();
    verifyBoundaryValidityAndOverlayContracts();
    verifyDynamicLayoutScaffolding();
    return 0;
}
