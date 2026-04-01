#include "ws/core/time_integrator.hpp"
#include <stdexcept>

namespace ws {

void EulerExplicit::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    if (state.data.size() != derivatives.size()) {
        throw std::runtime_error("Euler Explicit: State and derivative array sizes mismatch");
    }
    // Simple 1st-order forward integration 
    for (size_t i = 0; i < state.data.size(); ++i) {
        state.data[i] += derivatives[i] * dt;
    }
}

void RK4::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    if (state.data.size() != derivatives.size()) {
        throw std::runtime_error("RK4: State and derivative array sizes mismatch");
    }
    // Note: True RK4 requires k1, k2, k3, k4 derived by evaluating the IR solver iteratively. 
    // This signature only offers a single derivative array (k1 approximation).
    // The Scheduler DAG driver in Phase 2 will restructure evaluating these fields 4 times.
    for (size_t i = 0; i < state.data.size(); ++i) {
        state.data[i] += derivatives[i] * dt;
    }
}

TimeIntegratorRegistry& TimeIntegratorRegistry::instance() {
    static TimeIntegratorRegistry inst;
    return inst;
}

TimeIntegratorRegistry::TimeIntegratorRegistry() {
    registerIntegrator("Euler Explicit", std::make_unique<EulerExplicit>());
    registerIntegrator("RK4", std::make_unique<RK4>());
}

void TimeIntegratorRegistry::registerIntegrator(const std::string& id, std::unique_ptr<TimeIntegrator> integrator) {
    registry[id] = std::move(integrator);
}

std::shared_ptr<TimeIntegrator> TimeIntegratorRegistry::get(const std::string& id) const {
    auto it = registry.find(id);
    if (it != registry.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace ws
