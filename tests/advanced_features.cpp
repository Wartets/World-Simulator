#include "ws/core/neighborhood.hpp"
#include "ws/core/random.hpp"
#include "ws/core/multidim_support.hpp"

#include <cassert>
#include <cmath>
#include <vector>
#include <cstdint>

namespace {

// Test Moore neighborhood definitions
void testMooreNeighborhoods() {
    // Test Moore4
    ws::NeighborhoodDefinition moore4(ws::NeighborhoodType::Moore4);
    assert(moore4.neighborCount() == 4);
    assert(moore4.typeName() == std::string("Moore4"));

    // Test Moore8
    ws::NeighborhoodDefinition moore8(ws::NeighborhoodType::Moore8);
    assert(moore8.neighborCount() == 8);
    assert(moore8.typeName() == std::string("Moore8"));

    // Test Moore12
    ws::NeighborhoodDefinition moore12(ws::NeighborhoodType::Moore12);
    assert(moore12.neighborCount() == 12);

    // Test Moore24
    ws::NeighborhoodDefinition moore24(ws::NeighborhoodType::Moore24);
    assert(moore24.neighborCount() == 24);

    // Verify offset consistency
    assert(moore4.neighborOffset(0) == std::make_pair(0, -1));  // North
    assert(moore4.neighborOffset(1) == std::make_pair(0, 1));   // South
    assert(moore4.neighborOffset(2) == std::make_pair(1, 0));   // East
    assert(moore4.neighborOffset(3) == std::make_pair(-1, 0));  // West
}

// Test custom neighborhoods
void testCustomNeighborhood() {
    ws::CustomNeighborhood custom;
    custom.name = "TestNeighborhood";
    custom.offsets = {{0, -1}, {0, 1}, {-1, 0}};

    ws::NeighborhoodDefinition neighborhood(custom);
    assert(neighborhood.type() == ws::NeighborhoodType::Custom);
    assert(neighborhood.neighborCount() == 3);
    assert(neighborhood.neighborOffset(0) == std::make_pair(0, -1));
}

// Test boundary handlers
void testBoundaryHandlers() {
    const std::uint32_t width = 5;
    const std::uint32_t height = 4;
    std::vector<float> grid(width * height);

    // Initialize grid with values
    for (std::size_t i = 0; i < grid.size(); ++i) {
        grid[i] = static_cast<float>(i);
    }

    // Test Periodic boundary
    ws::BoundaryHandler periodicBC(ws::BoundaryCondition::Periodic);
    assert(periodicBC.sampleWithBoundary(grid.data(), width, height, -1, 0) ==
           grid[4]);  // Wraps to x=4
    assert(periodicBC.sampleWithBoundary(grid.data(), width, height, width, 0) ==
           grid[0]);  // Wraps to x=0

    // Test Dirichlet boundary
    ws::BoundaryHandler dirichletBC(ws::BoundaryCondition::Dirichlet, 99.0f);
    assert(dirichletBC.sampleWithBoundary(grid.data(), width, height, -1, 0) == 99.0f);
    assert(dirichletBC.sampleWithBoundary(grid.data(), width, height, width, 0) == 99.0f);

    // Test Neumann boundary
    ws::BoundaryHandler neumannBC(ws::BoundaryCondition::Neumann);
    assert(neumannBC.sampleWithBoundary(grid.data(), width, height, -1, 0) ==
           grid[0]);  // Clamps to x=0
    assert(neumannBC.sampleWithBoundary(grid.data(), width, height, width, 0) ==
           grid[4]);  // Clamps to x=4

    // Test Reflecting boundary
    ws::BoundaryHandler reflectingBC(ws::BoundaryCondition::Reflecting);
    float reflected = reflectingBC.sampleWithBoundary(grid.data(), width, height, -1, 0);
    assert(reflected == grid[1]);  // Reflects to x=1

    // Test in-bounds access
    assert(periodicBC.sampleWithBoundary(grid.data(), width, height, 2, 1) == grid[1 * width + 2]);
}

// Callback for stencil tests
static int stencilCellCount = 0;
void stencilCallback(
    std::uint32_t x,
    std::uint32_t y,
    const float* neighborValues,
    std::size_t neighborCount,
    void* userData) {
    (void)x;
    (void)y;
    (void)neighborValues;
    (void)userData;
    assert(neighborCount == 4);
    stencilCellCount++;
}

// Test neighborhood stencil application
void testNeighborhoodStencil() {
    const std::uint32_t width = 5;
    const std::uint32_t height = 3;
    std::vector<float> input(width * height);

    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i + 1);  // 1, 2, 3, ..., 15
    }

    ws::NeighborhoodDefinition moore4(ws::NeighborhoodType::Moore4);
    ws::BoundaryHandler dirichletBC(ws::BoundaryCondition::Dirichlet, 0.0f);
    ws::NeighborhoodStencil stencil(moore4, dirichletBC);

    // Reset and apply stencil
    stencilCellCount = 0;
    stencil.apply(input.data(), width, height, stencilCallback, nullptr);
    assert(stencilCellCount == static_cast<int>(width * height));
}

// Test deterministic RNG
void testDeterministicRNG() {
    std::uint64_t globalSeed = 12345;
    ws::random::DeterministicRNG rng(globalSeed);

    // Seed the same cell twice and verify reproducibility
    rng.seedCell(0, 0, 0);
    float value1 = rng.uniform();

    rng.seedCell(0, 0, 0);
    float value2 = rng.uniform();

    assert(value1 == value2);
    assert(value1 >= 0.0f && value1 <= 1.0f);

    // Different cell should produce different value
    rng.seedCell(1, 0, 0);
    float value3 = rng.uniform();
    assert(value3 != value1);  // Different cell, different random value
}

// Test RNG reproducibility across steps
void testRNGReproducibility() {
    std::uint64_t globalSeed = 42;

    ws::random::DeterministicRNG rng1(globalSeed);
    rng1.seedCell(5, 5, 10);
    float v1_step1 = rng1.uniform();
    float v1_step2 = rng1.uniform();

    ws::random::DeterministicRNG rng2(globalSeed);
    rng2.seedCell(5, 5, 10);
    float v2_step1 = rng2.uniform();
    float v2_step2 = rng2.uniform();

    assert(v1_step1 == v2_step1);
    assert(v1_step2 == v2_step2);
}

// Test Gaussian distribution
void testGaussianRNG() {
    std::uint64_t globalSeed = 999;
    ws::random::DeterministicRNG rng(globalSeed);
    rng.seedCell(0, 0, 0);

    // Generate Gaussian samples and verify they're centered around mean
    float mean = 10.0f;
    float stddev = 2.0f;
    float sum = 0.0f;
    const int samples = 100;

    for (int i = 0; i < samples; ++i) {
        float val = rng.gaussian(mean, stddev);
        sum += val;
    }

    float sampleMean = sum / samples;
    // With 100 samples, should be reasonably close to expected mean
    assert(std::fabs(sampleMean - mean) < 1.0f);
}

// Test multi-dimensional grid dimensions
void testGridDimensions() {
    // 2D grid
    ws::GridDimensions grid2d(10, 20);
    assert(grid2d.cellCount() == 200);
    assert(grid2d.dimensionCount() == 2);

    // 3D grid
    ws::GridDimensions grid3d(8, 16, 4);
    assert(grid3d.cellCount() == 512);
    assert(grid3d.dimensionCount() == 3);

    // Validate boundary conditions match dimensions
    grid2d.validate();
    grid3d.validate();
}

// Test grid strides
void testGridStrides() {
    ws::GridDimensions grid(4, 3, 2);  // 4x3x2 grid = 24 cells

    ws::GridStrides strides(grid);

    // Test linear index computation
    std::vector<std::uint32_t> coords = {0, 0, 0};
    assert(strides.toLinearIndex(coords) == 0);

    coords = {1, 0, 0};
    assert(strides.toLinearIndex(coords) == 6);  // stride[0] = 3*2 = 6

    coords = {0, 1, 0};
    assert(strides.toLinearIndex(coords) == 2);  // stride[1] = 2

    coords = {0, 0, 1};
    assert(strides.toLinearIndex(coords) == 1);  // stride[2] = 1

    coords = {1, 2, 1};
    assert(strides.toLinearIndex(coords) == 6 + 4 + 1);  // 11
}

// Test coordinate conversion
void testCoordinateConversion() {
    ws::GridDimensions grid(5, 4, 3);
    ws::GridStrides strides(grid);

    // Test round-trip conversion
    std::vector<std::uint32_t> original = {2, 1, 2};
    std::uint64_t index = strides.toLinearIndex(original);
    std::vector<std::uint32_t> recovered = strides.toCoordinates(index, grid);

    assert(original == recovered);
}

// Test multi-dimensional boundary resolver
void testMultiDimBoundaryResolver() {
    ws::GridDimensions grid(5, 4);
    grid.boundaryConditions[0] = ws::BoundaryCondition::Periodic;
    grid.boundaryConditions[1] = ws::BoundaryCondition::Neumann;

    ws::MultiDimBoundaryResolver resolver(grid);

    // Test periodic wrapping in x
    auto resolved = resolver.resolveDimension(-1, 0);
    assert(resolved == 4);  // Wraps to x=4

    // Test Neumann clamping in y
    resolved = resolver.resolveDimension(10, 1);
    assert(resolved == 3);  // Clamps to y=3

    // Test coordinate vector resolution
    std::vector<std::int64_t> coords = {-1, 10};
    auto resolvedCoords = resolver.resolveCoordinates(coords);
    assert(resolvedCoords[0] == 4);
    assert(resolvedCoords[1] == 3);
}

// Test noise functions
void testNoiseConsistency() {
    // Value noise should be consistent for same input
    float noise1 = ws::random::noise::valueNoise2D(1.5f, 2.5f, 42);
    float noise2 = ws::random::noise::valueNoise2D(1.5f, 2.5f, 42);
    assert(noise1 == noise2);

    // Different seed should give different noise
    float noise3 = ws::random::noise::valueNoise2D(1.5f, 2.5f, 43);
    assert(noise1 != noise3);

    // Perlin noise should also be consistent
    float perlin1 = ws::random::noise::perlin2D(1.0f, 1.0f, 100);
    float perlin2 = ws::random::noise::perlin2D(1.0f, 1.0f, 100);
    assert(perlin1 == perlin2);

    // Values should be in reasonable range
    assert(perlin1 >= -1.0f && perlin1 <= 1.0f);
}

} // namespace

int main() {
    testMooreNeighborhoods();
    testCustomNeighborhood();
    testBoundaryHandlers();
    testNeighborhoodStencil();
    testDeterministicRNG();
    testRNGReproducibility();
    testGaussianRNG();
    testGridDimensions();
    testGridStrides();
    testCoordinateConversion();
    testMultiDimBoundaryResolver();
    testNoiseConsistency();
    return 0;
}
