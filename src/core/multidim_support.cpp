#include "ws/core/multidim_support.hpp"

#include <algorithm>

namespace ws {

MultiDimBoundaryResolver::MultiDimBoundaryResolver(const GridDimensions& grid)
    : grid_(grid) {}

std::uint32_t MultiDimBoundaryResolver::resolveDimension(
    std::int64_t coord,
    std::size_t dimension) const {
    if (dimension >= grid_.sizes.size()) {
        return 0;
    }

    const std::uint32_t size = grid_.sizes[dimension];
    const BoundaryCondition bc = grid_.boundaryConditions[dimension];

    if (coord >= 0 && coord < static_cast<std::int64_t>(size)) {
        return static_cast<std::uint32_t>(coord);
    }

    switch (bc) {
        case BoundaryCondition::Periodic: {
            const std::int64_t mod = static_cast<std::int64_t>(size);
            const std::int64_t wrapped = ((coord % mod) + mod) % mod;
            return static_cast<std::uint32_t>(wrapped);
        }

        case BoundaryCondition::Reflecting: {
            if (size <= 1) {
                return 0;
            }
            const std::int64_t period = static_cast<std::int64_t>(2 * size - 2);
            std::int64_t wrapped = coord % period;
            if (wrapped < 0) {
                wrapped += period;
            }
            if (wrapped >= static_cast<std::int64_t>(size)) {
                wrapped = period - wrapped;
            }
            return static_cast<std::uint32_t>(wrapped);
        }

        case BoundaryCondition::Dirichlet:
        case BoundaryCondition::Absorbing:
            // Return an invalid marker; caller should check
            return 0;

        case BoundaryCondition::Neumann: {
            // Zero-flux: clamp to boundary
            return static_cast<std::uint32_t>(std::clamp<std::int64_t>(coord, 0, static_cast<std::int64_t>(size) - 1));
        }
    }

    return 0;
}

std::vector<std::uint32_t> MultiDimBoundaryResolver::resolveCoordinates(
    const std::vector<std::int64_t>& coords) const {
    std::vector<std::uint32_t> resolved;
    resolved.reserve(coords.size());

    for (std::size_t i = 0; i < coords.size(); ++i) {
        resolved.push_back(resolveDimension(coords[i], i));
    }

    return resolved;
}

} // namespace ws
