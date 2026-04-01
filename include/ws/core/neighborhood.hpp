#pragma once

#include "ws/core/spatial_scheme.hpp"

#include <cstdint>
#include <vector>
#include <utility>
#include <string>

namespace ws {

enum class NeighborhoodType : std::uint8_t {
    Moore4 = 0,      // N, S, E, W (4-connected)
    Moore8 = 1,      // + diagonals (8-connected)
    Moore12 = 2,     // Extended Moore (12 cells)
    Moore24 = 3,     // Extended (24 cells)
    Custom = 4       // User-defined offset list
};

struct CustomNeighborhood {
    std::string name;
    std::vector<std::pair<int, int>> offsets;  // List of (dx, dy) offsets
};

class NeighborhoodDefinition {
public:
    explicit NeighborhoodDefinition(NeighborhoodType type);
    explicit NeighborhoodDefinition(const CustomNeighborhood& custom);

    [[nodiscard]] NeighborhoodType type() const noexcept { return type_; }
    [[nodiscard]] const std::vector<std::pair<int, int>>& offsets() const noexcept { return offsets_; }
    [[nodiscard]] std::size_t neighborCount() const noexcept { return offsets_.size(); }

    // Return the offset for neighbor at index i
    [[nodiscard]] std::pair<int, int> neighborOffset(std::size_t i) const noexcept;

    // Get display name for the neighborhood type
    [[nodiscard]] const char* typeName() const noexcept;

private:
    NeighborhoodType type_;
    std::vector<std::pair<int, int>> offsets_;

    void initMooreOffsets(NeighborhoodType type);
};

class BoundaryHandler {
public:
    explicit BoundaryHandler(
        BoundaryCondition bc,
        float boundaryValue = 0.0f) noexcept;

    // Sample a value at (x, y) with boundary handling
    // Returns the value at the requested location, applying boundary conditions if out of bounds
    [[nodiscard]] float sampleWithBoundary(
        const float* data,
        std::uint32_t width,
        std::uint32_t height,
        std::int64_t x,
        std::int64_t y) const noexcept;

    [[nodiscard]] BoundaryCondition condition() const noexcept { return bc_; }
    [[nodiscard]] float boundaryValue() const noexcept { return boundaryValue_; }

    void setBoundaryValue(float value) noexcept { boundaryValue_ = value; }

private:
    BoundaryCondition bc_;
    float boundaryValue_;

    [[nodiscard]] std::uint32_t wrapIndex(std::int64_t value, std::uint32_t extent) const noexcept;
    [[nodiscard]] std::uint32_t reflectIndex(std::int64_t value, std::uint32_t extent) const noexcept;
};

// Utility to apply a stencil operation using a neighborhood and boundary handler
class NeighborhoodStencil {
public:
    NeighborhoodStencil(
        const NeighborhoodDefinition& neighborhood,
        const BoundaryHandler& boundary) noexcept;

    // Apply a stencil operation to all interior points
    // callback(x, y, neighborhood_values) is called for each cell
    using StencilCallback = void (*)(
        std::uint32_t x,
        std::uint32_t y,
        const float* neighborValues,
        std::size_t neighborCount,
        void* userData);

    void apply(
        const float* input,
        std::uint32_t width,
        std::uint32_t height,
        StencilCallback callback,
        void* userData) const noexcept;

private:
    const NeighborhoodDefinition& neighborhood_;
    const BoundaryHandler& boundary_;
};

} // namespace ws
