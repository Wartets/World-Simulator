#include "ws/core/neighborhood.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ws {

NeighborhoodDefinition::NeighborhoodDefinition(NeighborhoodType type)
    : type_(type) {
    initMooreOffsets(type);
}

NeighborhoodDefinition::NeighborhoodDefinition(const CustomNeighborhood& custom)
    : type_(NeighborhoodType::Custom), offsets_(custom.offsets) {}

void NeighborhoodDefinition::initMooreOffsets(NeighborhoodType type) {
    offsets_.clear();

    switch (type) {
        case NeighborhoodType::Moore4:
            // N, S, E, W
            offsets_.push_back({0, -1});  // North
            offsets_.push_back({0, 1});   // South
            offsets_.push_back({1, 0});   // East
            offsets_.push_back({-1, 0});  // West
            break;

        case NeighborhoodType::Moore8:
            // Moore4 + diagonals
            offsets_.push_back({0, -1});  // North
            offsets_.push_back({0, 1});   // South
            offsets_.push_back({1, 0});   // East
            offsets_.push_back({-1, 0});  // West
            offsets_.push_back({1, -1});  // NE
            offsets_.push_back({-1, -1}); // NW
            offsets_.push_back({1, 1});   // SE
            offsets_.push_back({-1, 1});  // SW
            break;

        case NeighborhoodType::Moore12:
            // Extended Moore: distance 1 + distance 2 orthogonal
            offsets_.push_back({0, -1});  // North (d=1)
            offsets_.push_back({0, 1});   // South (d=1)
            offsets_.push_back({1, 0});   // East (d=1)
            offsets_.push_back({-1, 0});  // West (d=1)
            offsets_.push_back({1, -1});  // NE (d=1)
            offsets_.push_back({-1, -1}); // NW (d=1)
            offsets_.push_back({1, 1});   // SE (d=1)
            offsets_.push_back({-1, 1});  // SW (d=1)
            offsets_.push_back({0, -2});  // N (d=2)
            offsets_.push_back({0, 2});   // S (d=2)
            offsets_.push_back({2, 0});   // E (d=2)
            offsets_.push_back({-2, 0});  // W (d=2)
            break;

        case NeighborhoodType::Moore24:
            // All cells within Chebyshev distance 2
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (dx == 0 && dy == 0) continue;  // Skip center
                    offsets_.push_back({dx, dy});
                }
            }
            break;

        case NeighborhoodType::Custom:
            // Custom offsets should already be set
            break;
    }
}

std::pair<int, int> NeighborhoodDefinition::neighborOffset(std::size_t i) const noexcept {
    if (i < offsets_.size()) {
        return offsets_[i];
    }
    return {0, 0};
}

const char* NeighborhoodDefinition::typeName() const noexcept {
    switch (type_) {
        case NeighborhoodType::Moore4:
            return "Moore4";
        case NeighborhoodType::Moore8:
            return "Moore8";
        case NeighborhoodType::Moore12:
            return "Moore12";
        case NeighborhoodType::Moore24:
            return "Moore24";
        case NeighborhoodType::Custom:
            return "Custom";
    }
    return "Unknown";
}

BoundaryHandler::BoundaryHandler(BoundaryCondition bc, float boundaryValue) noexcept
    : bc_(bc), boundaryValue_(boundaryValue) {}

std::uint32_t BoundaryHandler::wrapIndex(std::int64_t value, std::uint32_t extent) const noexcept {
    if (extent == 0) {
        return 0;
    }
    const std::int64_t mod = static_cast<std::int64_t>(extent);
    const std::int64_t wrapped = ((value % mod) + mod) % mod;
    return static_cast<std::uint32_t>(wrapped);
}

std::uint32_t BoundaryHandler::reflectIndex(std::int64_t value, std::uint32_t extent) const noexcept {
    if (extent <= 1u) {
        return 0u;
    }

    const std::int64_t period = static_cast<std::int64_t>(2u * extent - 2u);
    std::int64_t wrapped = value % period;
    if (wrapped < 0) {
        wrapped += period;
    }
    if (wrapped >= static_cast<std::int64_t>(extent)) {
        wrapped = period - wrapped;
    }
    return static_cast<std::uint32_t>(wrapped);
}

float BoundaryHandler::sampleWithBoundary(
    const float* data,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t x,
    std::int64_t y) const noexcept {
    if (data == nullptr || width == 0 || height == 0) {
        return boundaryValue_;
    }

    const auto inside = [width, height](std::int64_t ix, std::int64_t iy) noexcept {
        return ix >= 0 && iy >= 0 && ix < static_cast<std::int64_t>(width) && iy < static_cast<std::int64_t>(height);
    };

    if (inside(x, y)) {
        return data[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    }

    switch (bc_) {
        case BoundaryCondition::Periodic: {
            const auto rx = wrapIndex(x, width);
            const auto ry = wrapIndex(y, height);
            return data[static_cast<std::size_t>(ry) * width + rx];
        }

        case BoundaryCondition::Reflecting: {
            const auto rx = reflectIndex(x, width);
            const auto ry = reflectIndex(y, height);
            return data[static_cast<std::size_t>(ry) * width + rx];
        }

        case BoundaryCondition::Dirichlet:
        case BoundaryCondition::Absorbing:
            return boundaryValue_;

        case BoundaryCondition::Neumann: {
            // Zero-flux: return the edge value (clamp coordinates)
            const auto cx = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(width) - 1));
            const auto cy = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1));
            return data[static_cast<std::size_t>(cy) * width + cx];
        }
    }

    return boundaryValue_;
}

NeighborhoodStencil::NeighborhoodStencil(
    const NeighborhoodDefinition& neighborhood,
    const BoundaryHandler& boundary) noexcept
    : neighborhood_(neighborhood), boundary_(boundary) {}

void NeighborhoodStencil::apply(
    const float* input,
    std::uint32_t width,
    std::uint32_t height,
    StencilCallback callback,
    void* userData) const noexcept {
    if (input == nullptr || callback == nullptr || width == 0 || height == 0) {
        return;
    }

    const std::size_t neighborCount = neighborhood_.neighborCount();
    std::vector<float> neighborValues(neighborCount);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            for (std::size_t i = 0; i < neighborCount; ++i) {
                const auto [dx, dy] = neighborhood_.neighborOffset(i);
                const std::int64_t nx = static_cast<std::int64_t>(x) + dx;
                const std::int64_t ny = static_cast<std::int64_t>(y) + dy;
                neighborValues[i] = boundary_.sampleWithBoundary(input, width, height, nx, ny);
            }

            callback(x, y, neighborValues.data(), neighborCount, userData);
        }
    }
}

} // namespace ws
