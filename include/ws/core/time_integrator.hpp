#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <stdexcept>

namespace ws {

// =============================================================================
// State Buffer
// =============================================================================

// Container for simulation state data.
struct StateBuffer {
    std::vector<float> data;
};

// =============================================================================
// Time Integrator
// =============================================================================

// Base class for time integration schemes.
class TimeIntegrator {
public:
    virtual ~TimeIntegrator() = default;
    
    // Returns the name of the integrator.
    virtual std::string name() const = 0;
    // Returns the order of accuracy (1 for Euler, 4 for RK4).
    virtual int order() const = 0;
    
    // Performs a single time step.
    virtual void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) = 0;
};

// =============================================================================
// Euler Explicit Integrator
// =============================================================================

// First-order explicit Euler method.
class EulerExplicit : public TimeIntegrator {
public:
    std::string name() const override { return "Euler Explicit"; }
    int order() const override { return 1; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// Second-order midpoint Runge-Kutta method.
class RK2Midpoint : public TimeIntegrator {
public:
    std::string name() const override { return "RK2 Midpoint"; }
    int order() const override { return 2; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// Third-order Runge-Kutta method.
class RK3Heun : public TimeIntegrator {
public:
    std::string name() const override { return "RK3 Heun"; }
    int order() const override { return 3; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// Semi-implicit Euler variant.
class SemiImplicitEuler : public TimeIntegrator {
public:
    std::string name() const override { return "Semi-Implicit Euler"; }
    int order() const override { return 1; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// Velocity Verlet variant.
class VelocityVerlet : public TimeIntegrator {
public:
    std::string name() const override { return "Velocity Verlet"; }
    int order() const override { return 2; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// Crank-Nicolson scheme.
class CrankNicolson : public TimeIntegrator {
public:
    std::string name() const override { return "Crank-Nicolson"; }
    int order() const override { return 2; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// =============================================================================
// Runge-Kutta 4 Integrator
// =============================================================================

// Fourth-order Runge-Kutta method.
class RK4 : public TimeIntegrator {
public:
    std::string name() const override { return "RK4"; }
    int order() const override { return 4; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

// =============================================================================
// Time Integrator Registry
// =============================================================================

// Registry for time integration schemes.
class TimeIntegratorRegistry {
public:
    // Returns the singleton instance.
    static TimeIntegratorRegistry& instance();
    
    // Registers an integrator with an identifier.
    void registerIntegrator(const std::string& id, std::unique_ptr<TimeIntegrator> integrator);
    void registerIntegrator(const std::string& id, std::shared_ptr<TimeIntegrator> integrator);
    void registerAlias(const std::string& aliasId, const std::string& canonicalId);
    // Retrieves an integrator by identifier.
    std::shared_ptr<TimeIntegrator> get(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> availableIds() const;
    
private:
    TimeIntegratorRegistry();
    std::map<std::string, std::shared_ptr<TimeIntegrator>> registry;
    std::map<std::string, std::string> aliases;
};

} // namespace ws
