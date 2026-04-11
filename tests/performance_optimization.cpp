#include "ws/core/spatial_scheme.hpp"
#include "ws/core/state_store.hpp"
#include "ws/core/vectorized_ops.hpp"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace {


// Use the unified helper for test reference as well
float sampleBoundary(
    const std::vector<float>& input,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t x,
    std::int64_t y,
    ws::BoundaryCondition bc) {
    return ws::vectorized::detail::sampleBoundaryImpl(input.data(), width, height, x, y, bc, 0.0f);
}

std::vector<float> referenceLaplacian(const std::vector<float>& input, std::uint32_t width, std::uint32_t height, ws::BoundaryCondition bc) {
    std::vector<float> output(input.size(), 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float center = sampleBoundary(input, width, height, x, y, bc);
            const float north = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) - 1, bc);
            const float south = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) + 1, bc);
            const float east = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(y), bc);
            const float west = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(y), bc);
            output[static_cast<std::size_t>(y) * width + x] = north + south + east + west - 4.0f * center;
        }
    }
    return output;
}

void referenceGradient(
    const std::vector<float>& input,
    std::vector<float>& outX,
    std::vector<float>& outY,
    std::uint32_t width,
    std::uint32_t height,
    ws::BoundaryCondition bc) {
    outX.assign(input.size(), 0.0f);
    outY.assign(input.size(), 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float east = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) + 1, static_cast<std::int64_t>(y), bc);
            const float west = sampleBoundary(input, width, height, static_cast<std::int64_t>(x) - 1, static_cast<std::int64_t>(y), bc);
            const float north = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) - 1, bc);
            const float south = sampleBoundary(input, width, height, static_cast<std::int64_t>(x), static_cast<std::int64_t>(y) + 1, bc);
            const std::size_t idx = static_cast<std::size_t>(y) * width + x;
            outX[idx] = 0.5f * (east - west);
            outY[idx] = 0.5f * (south - north);
        }
    }
}

void verifyVectorizedArithmeticAndClamp() {
    alignas(32) float a[] = {1.0f, -3.0f, 5.5f, 8.0f, 11.0f, -2.0f, 0.0f, 4.0f};
    alignas(32) float b[] = {2.0f, 4.0f, -1.5f, 1.0f, 0.5f, 5.0f, 2.0f, -2.0f};
    float out[8] = {};

    ws::vectorized::add(out, a, b, 8);
    assert(out[0] == 3.0f && out[1] == 1.0f && out[2] == 4.0f && out[7] == 2.0f);

    ws::vectorized::subtract(out, a, b, 8);
    assert(out[0] == -1.0f && out[1] == -7.0f && out[2] == 7.0f && out[7] == 6.0f);

    ws::vectorized::multiply(out, a, b, 8);
    assert(out[0] == 2.0f && out[1] == -12.0f && out[2] == -8.25f && out[7] == -8.0f);

    ws::vectorized::divide(out, a, b, 8);
    assert(std::fabs(out[0] - 0.5f) < 1e-6f);
    assert(std::fabs(out[3] - 8.0f) < 1e-6f);

    ws::vectorized::clampInPlace(out, 8, -1.0f, 1.0f);
    for (float value : out) {
        assert(value >= -1.0f && value <= 1.0f);
    }
}

void verifyStateStoreClamp() {
    ws::StateStore store(ws::GridSpec{4, 2}, ws::BoundaryMode::Clamp, ws::GridTopologyBackend::Cartesian2D, ws::MemoryLayoutPolicy{64, 4, 1});
    store.allocateScalarField(ws::VariableSpec{0, "temperature"});

    ws::StateStore::WriteSession writer(store, "perf_test", {"temperature"});
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const float value = static_cast<float>(x) - 2.5f + static_cast<float>(y) * 4.0f;
            writer.setScalar("temperature", ws::Cell{x, y}, value);
        }
    }

    store.clampField("temperature", -0.5f, 1.5f);
    const auto& values = store.scalarField("temperature");
    for (std::size_t i = 0; i < 8; ++i) {
        assert(values[i] >= -0.5f && values[i] <= 1.5f);
    }
}

void verifySpatialSchemesAgainstReference() {
    const std::uint32_t width = 5;
    const std::uint32_t height = 4;
    std::vector<float> input(width * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            input[static_cast<std::size_t>(y) * width + x] = static_cast<float>(y * 10u + x);
        }
    }

    ws::SecondOrderLaplacian2D laplacian;
    ws::CentralDifferenceGradient2D gradient;

    std::vector<float> out(width * height, 0.0f);
    std::vector<float> refOut(width * height, 0.0f);
    std::vector<float> gradX(width * height, 0.0f);
    std::vector<float> gradY(width * height, 0.0f);
    std::vector<float> refGradX(width * height, 0.0f);
    std::vector<float> refGradY(width * height, 0.0f);

    for (const auto bc : {ws::BoundaryCondition::Periodic, ws::BoundaryCondition::Dirichlet, ws::BoundaryCondition::Neumann, ws::BoundaryCondition::Reflecting, ws::BoundaryCondition::Absorbing}) {
        laplacian.apply(input, out, width, height, bc);
        refOut = referenceLaplacian(input, width, height, bc);
        for (std::size_t i = 0; i < out.size(); ++i) {
            assert(std::fabs(out[i] - refOut[i]) < 1e-5f);
        }

        gradient.apply(input, gradX, gradY, width, height, bc);
        referenceGradient(input, refGradX, refGradY, width, height, bc);
        for (std::size_t i = 0; i < gradX.size(); ++i) {
            assert(std::fabs(gradX[i] - refGradX[i]) < 1e-5f);
            assert(std::fabs(gradY[i] - refGradY[i]) < 1e-5f);
        }
    }
}

} // namespace

int main() {
    verifyVectorizedArithmeticAndClamp();
    verifyStateStoreClamp();
    verifySpatialSchemesAgainstReference();
    return 0;
}
