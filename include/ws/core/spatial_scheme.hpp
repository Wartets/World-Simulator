#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>

namespace ws {

// =============================================================================
// Boundary Condition
// =============================================================================

// Types of boundary conditions for spatial operators.
enum class BoundaryCondition : std::uint8_t {
    Periodic = 0,    // Values wrap around (toroidal topology).
    Dirichlet = 1,   // Fixed value at boundary (zero or specified).
    Neumann = 2,     // Zero gradient at boundary.
    Reflecting = 3,  // Mirror reflection at boundary.
    Absorbing = 4    // Waves absorbed at boundary.
};

// =============================================================================
// Spatial Scheme
// =============================================================================

// Base class for spatial discretization schemes.
class SpatialScheme {
public:
    virtual ~SpatialScheme() = default;
    // Returns the name of the scheme.
    virtual std::string name() const = 0;
};

// =============================================================================
// Laplacian Scheme
// =============================================================================

// Computes the Laplacian (second spatial derivative) of a field.
class LaplacianScheme : public SpatialScheme {
public:
    // Applies the Laplacian operator to the input grid.
    virtual void apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const = 0;
};

// =============================================================================
// Gradient Scheme
// =============================================================================

// Computes the gradient (first spatial derivatives) of a field.
class GradientScheme : public SpatialScheme {
public:
    // Computes spatial gradients in x and y directions.
    virtual void apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const = 0;
};

// =============================================================================
// Spatial Scheme Registry
// =============================================================================

// Registry for spatial discretization schemes.
class SpatialSchemeRegistry {
public:
    // Returns the singleton instance.
    static SpatialSchemeRegistry& instance();
    
    // Registers a Laplacian scheme with an identifier.
    void registerLaplacian(const std::string& id, std::unique_ptr<LaplacianScheme> scheme);
    // Registers a Gradient scheme with an identifier.
    void registerGradient(const std::string& id, std::unique_ptr<GradientScheme> scheme);
    
    // Retrieves a Laplacian scheme by identifier.
    std::shared_ptr<LaplacianScheme> getLaplacian(const std::string& id) const;
    // Retrieves a Gradient scheme by identifier.
    std::shared_ptr<GradientScheme> getGradient(const std::string& id) const;
    
private:
    SpatialSchemeRegistry();
    std::map<std::string, std::shared_ptr<LaplacianScheme>> laplacians;
    std::map<std::string, std::shared_ptr<GradientScheme>> gradients;
};

// =============================================================================
// Default Implementations
// =============================================================================

// Second-order accurate 2D Laplacian using 5-point stencil.
class SecondOrderLaplacian2D : public LaplacianScheme {
public:
    std::string name() const override { return "SecondOrder_Laplacian2D"; }
    void apply(const std::vector<float>& input, std::vector<float>& output, uint32_t width, uint32_t height, BoundaryCondition bc) const override;
};

// Central difference gradient scheme for 2D grids.
class CentralDifferenceGradient2D : public GradientScheme {
public:
    std::string name() const override { return "CentralDifference_Gradient2D"; }
    void apply(const std::vector<float>& input, std::vector<float>& out_x, std::vector<float>& out_y, uint32_t width, uint32_t height, BoundaryCondition bc) const override;
};

} // namespace ws
