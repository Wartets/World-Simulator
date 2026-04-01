#include "ws/core/spatial_scheme.hpp"
#include "ws/core/vectorized_ops.hpp"
#include <stdexcept>
#include <algorithm>

namespace ws {

void SecondOrderLaplacian2D::apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || output.size() != input.size()) {
        throw std::runtime_error("SecondOrderLaplacian2D: Input/Output size mismatch");
    }

    vectorized::laplacian5Point2D(input.data(), output.data(), width, height, bc);
}

void CentralDifferenceGradient2D::apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || out_x.size() != input.size() || out_y.size() != input.size()) {
        throw std::runtime_error("CentralDifferenceGradient2D: Input/Output size mismatch");
    }

    vectorized::gradientCentralDifference2D(input.data(), out_x.data(), out_y.data(), width, height, bc);
}

SpatialSchemeRegistry& SpatialSchemeRegistry::instance() {
    static SpatialSchemeRegistry inst;
    return inst;
}

SpatialSchemeRegistry::SpatialSchemeRegistry() {
    registerLaplacian("Laplacian_5Point", std::make_unique<SecondOrderLaplacian2D>());
    registerGradient("Gradient_CentralDiff", std::make_unique<CentralDifferenceGradient2D>());
}

void SpatialSchemeRegistry::registerLaplacian(const std::string& id, std::unique_ptr<LaplacianScheme> scheme) {
    laplacians[id] = std::move(scheme);
}

void SpatialSchemeRegistry::registerGradient(const std::string& id, std::unique_ptr<GradientScheme> scheme) {
    gradients[id] = std::move(scheme);
}

std::shared_ptr<LaplacianScheme> SpatialSchemeRegistry::getLaplacian(const std::string& id) const {
    auto it = laplacians.find(id);
    return it != laplacians.end() ? it->second : nullptr;
}

std::shared_ptr<GradientScheme> SpatialSchemeRegistry::getGradient(const std::string& id) const {
    auto it = gradients.find(id);
    return it != gradients.end() ? it->second : nullptr;
}

} // namespace ws
