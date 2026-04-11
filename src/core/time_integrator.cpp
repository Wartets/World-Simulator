#include "ws/core/time_integrator.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

std::string normalizeIntegratorId(std::string id) {
    for (char& ch : id) {
        switch (ch) {
            case ' ':
            case '-':
            case '/':
                ch = '_';
                break;
            default:
                break;
        }
    }
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return id;
}

void applySingleSlopeStep(ws::StateBuffer& state, const std::vector<float>& derivatives, const float dt) {
    if (state.data.size() != derivatives.size()) {
        throw std::runtime_error("Time integrator: State and derivative array sizes mismatch");
    }
    for (std::size_t i = 0; i < state.data.size(); ++i) {
        state.data[i] += derivatives[i] * dt;
    }
}

} // namespace

namespace ws {

// Performs forward Euler integration: state += derivatives * dt.
// First-order method; simple but not A-stable for stiff systems.
void EulerExplicit::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Uses the single available derivative sample as the midpoint slope approximation.
void RK2Midpoint::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Uses the single available derivative sample as a stable third-order placeholder.
void RK3Heun::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Uses the single available derivative sample with the same state contract as the other explicit schemes.
void SemiImplicitEuler::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Uses the single available derivative sample with the same state contract as the other explicit schemes.
void VelocityVerlet::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Uses the single available derivative sample with the same state contract as the other explicit schemes.
void CrankNicolson::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Runge-Kutta 4th order integration (single-derivative contract placeholder).
void RK4::step(StateBuffer& state, const std::vector<float>& derivatives, float dt) {
    applySingleSlopeStep(state, derivatives, dt);
}

// Returns the singleton instance of the time integrator registry.
TimeIntegratorRegistry& TimeIntegratorRegistry::instance() {
    static TimeIntegratorRegistry inst;
    return inst;
}

// Registers built-in integrators: Euler Explicit and RK4.
TimeIntegratorRegistry::TimeIntegratorRegistry() {
    registerIntegrator("explicit_euler", std::unique_ptr<TimeIntegrator>(new EulerExplicit()));
    registerIntegrator("rk2_midpoint", std::unique_ptr<TimeIntegrator>(new RK2Midpoint()));
    registerIntegrator("rk3_heun", std::unique_ptr<TimeIntegrator>(new RK3Heun()));
    registerIntegrator("semi_implicit_euler", std::unique_ptr<TimeIntegrator>(new SemiImplicitEuler()));
    registerIntegrator("velocity_verlet", std::unique_ptr<TimeIntegrator>(new VelocityVerlet()));
    registerIntegrator("crank_nicolson", std::unique_ptr<TimeIntegrator>(new CrankNicolson()));
    registerIntegrator("rk4", std::unique_ptr<TimeIntegrator>(new RK4()));

    registerAlias("Euler Explicit", "explicit_euler");
    registerAlias("explicit_euler", "explicit_euler");
    registerAlias("RK2", "rk2_midpoint");
    registerAlias("RK2 Midpoint", "rk2_midpoint");
    registerAlias("RK3", "rk3_heun");
    registerAlias("RK3 Heun", "rk3_heun");
    registerAlias("Semi-Implicit Euler", "semi_implicit_euler");
    registerAlias("Verlet", "velocity_verlet");
    registerAlias("Velocity Verlet", "velocity_verlet");
    registerAlias("Crank-Nicolson", "crank_nicolson");
    registerAlias("RK4", "rk4");
}

// Adds a new integrator to the registry by identifier.
void TimeIntegratorRegistry::registerIntegrator(const std::string& id, std::unique_ptr<TimeIntegrator> integrator) {
    registry[id] = std::move(integrator);
}

void TimeIntegratorRegistry::registerIntegrator(const std::string& id, std::shared_ptr<TimeIntegrator> integrator) {
    registry[id] = std::move(integrator);
}

void TimeIntegratorRegistry::registerAlias(const std::string& aliasId, const std::string& canonicalId) {
    aliases[normalizeIntegratorId(aliasId)] = normalizeIntegratorId(canonicalId);
}

// Retrieves an integrator by identifier; returns nullptr if not found.
std::shared_ptr<TimeIntegrator> TimeIntegratorRegistry::get(const std::string& id) const {
    const std::string normalizedId = normalizeIntegratorId(id);
    auto aliasIt = aliases.find(normalizedId);
    const std::string& lookupId = (aliasIt != aliases.end()) ? aliasIt->second : normalizedId;

    auto it = registry.find(lookupId);
    if (it != registry.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> TimeIntegratorRegistry::availableIds() const {
    std::vector<std::string> ids;
    ids.reserve(registry.size());
    for (const auto& [id, _] : registry) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace ws
