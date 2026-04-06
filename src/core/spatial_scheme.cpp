#include "ws/core/spatial_scheme.hpp"
#include "ws/core/vectorized_ops.hpp"
#include <stdexcept>
#include <algorithm>

namespace ws {

// Applies 5-point Laplacian stencil to 2D input grid.
// Uses vectorized operations for interior cells; delegates boundary handling.
void SecondOrderLaplacian2D::apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || output.size() != input.size()) {
        throw std::runtime_error("SecondOrderLaplacian2D: Input/Output size mismatch");
    }

    vectorized::laplacian5Point2D(input.data(), output.data(), width, height, bc);
}

// Computes central difference gradient on 2D grid.
// Outputs x and y components as separate vectors.
void CentralDifferenceGradient2D::apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || out_x.size() != input.size() || out_y.size() != input.size()) {
        throw std::runtime_error("CentralDifferenceGradient2D: Input/Output size mismatch");
    }

    vectorized::gradientCentralDifference2D(input.data(), out_x.data(), out_y.data(), width, height, bc);
}

// Returns the singleton instance of the spatial scheme registry.
SpatialSchemeRegistry& SpatialSchemeRegistry::instance() {
    static SpatialSchemeRegistry inst;
    return inst;
}

// Registers default schemes: 5-point Laplacian and central difference gradient.
SpatialSchemeRegistry::SpatialSchemeRegistry() {
    registerLaplacian("Laplacian_5Point", std::make_unique<SecondOrderLaplacian2D>());
    registerGradient("Gradient_CentralDiff", std::make_unique<CentralDifferenceGradient2D>());
}

// Adds a Laplacian scheme to the registry.
void SpatialSchemeRegistry::registerLaplacian(const std::string& id, std::unique_ptr<LaplacianScheme> scheme) {
    laplacians[id] = std::move(scheme);
}

// Adds a gradient scheme to the registry.
void SpatialSchemeRegistry::registerGradient(const std::string& id, std::unique_ptr<GradientScheme> scheme) {
    gradients[id] = std::move(scheme);
}

// Retrieves a Laplacian scheme by identifier.
std::shared_ptr<LaplacianScheme> SpatialSchemeRegistry::getLaplacian(const std::string& id) const {
    auto it = laplacians.find(id);
    return it != laplacians.end() ? it->second : nullptr;
}

// Retrieves a gradient scheme by identifier.
std::shared_ptr<GradientScheme> SpatialSchemeRegistry::getGradient(const std::string& id) const {
    auto it = gradients.find(id);
    return it != gradients.end() ? it->second : nullptr;
}

} // namespace ws
