#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>

namespace ws {

enum class BoundaryCondition {
    Periodic, Dirichlet, Neumann, Reflecting, Absorbing
};

class SpatialScheme {
public:
    virtual ~SpatialScheme() = default;
    virtual std::string name() const = 0;
};

class LaplacianScheme : public SpatialScheme {
public:
    virtual void apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const = 0;
};

class GradientScheme : public SpatialScheme {
public:
    virtual void apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const = 0;
};

class SpatialSchemeRegistry {
public:
    static SpatialSchemeRegistry& instance();
    
    void registerLaplacian(const std::string& id, std::unique_ptr<LaplacianScheme> scheme);
    void registerGradient(const std::string& id, std::unique_ptr<GradientScheme> scheme);
    
    std::shared_ptr<LaplacianScheme> getLaplacian(const std::string& id) const;
    std::shared_ptr<GradientScheme> getGradient(const std::string& id) const;
    
private:
    SpatialSchemeRegistry();
    std::map<std::string, std::shared_ptr<LaplacianScheme>> laplacians;
    std::map<std::string, std::shared_ptr<GradientScheme>> gradients;
};

// Default Implementations for Phase 1
class SecondOrderLaplacian2D : public LaplacianScheme {
public:
    std::string name() const override { return "SecondOrder_Laplacian2D"; }
    void apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const override;
};

class CentralDifferenceGradient2D : public GradientScheme {
public:
    std::string name() const override { return "CentralDifference_Gradient2D"; }
    void apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const override;
};

} // namespace ws
