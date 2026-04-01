#include "ws/core/spatial_scheme.hpp"
#include <stdexcept>
#include <algorithm>

namespace ws {

// Helper to handle grid boundaries
inline int getIndex(int x, int y, int width, int height, BoundaryCondition bc) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return y * width + x;
    }
    
    if (bc == BoundaryCondition::Periodic) {
        x = (x % width + width) % width;
        y = (y % height + height) % height;
        return y * width + x;
    }
    
    // For Neumann/Dirichlet simplicity in this mock, we map to edge element natively
    // A true Dirichlet would inject a configured outside value, while Neumann 0 copies the adjacent cell
    x = std::max(0, std::min(x, width - 1));
    y = std::max(0, std::min(y, height - 1));
    return y * width + x;
}

void SecondOrderLaplacian2D::apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || output.size() != input.size()) {
        throw std::runtime_error("SecondOrderLaplacian2D: Input/Output size mismatch");
    }
    
    for (int y = 0; y < (int)height; ++y) {
        for (int x = 0; x < (int)width; ++x) {
            float center = input[y * width + x];
            float n = input[getIndex(x, y - 1, width, height, bc)];
            float s = input[getIndex(x, y + 1, width, height, bc)];
            float e = input[getIndex(x + 1, y, width, height, bc)];
            float w = input[getIndex(x - 1, y, width, height, bc)];
            
            output[y * width + x] = n + s + e + w - 4.0f * center;
        }
    }
}

void CentralDifferenceGradient2D::apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const {
    if (input.size() != width * height || out_x.size() != input.size() || out_y.size() != input.size()) {
        throw std::runtime_error("CentralDifferenceGradient2D: Input/Output size mismatch");
    }
    
    for (int y = 0; y < (int)height; ++y) {
        for (int x = 0; x < (int)width; ++x) {
            float n = input[getIndex(x, y - 1, width, height, bc)];
            float s = input[getIndex(x, y + 1, width, height, bc)];
            float e = input[getIndex(x + 1, y, width, height, bc)];
            float w = input[getIndex(x - 1, y, width, height, bc)];
            
            out_x[y * width + x] = (e - w) / 2.0f;
            out_y[y * width + x] = (s - n) / 2.0f;
        }
    }
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
