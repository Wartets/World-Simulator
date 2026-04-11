#include "ws/core/time_integrator.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

using namespace ws;

void test_integrator_registry_catalog() {
    auto& registry = TimeIntegratorRegistry::instance();
    const auto ids = registry.availableIds();

    assert(std::find(ids.begin(), ids.end(), "crank_nicolson") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "explicit_euler") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "rk2_midpoint") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "rk3_heun") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "rk4") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "semi_implicit_euler") != ids.end());
    assert(std::find(ids.begin(), ids.end(), "velocity_verlet") != ids.end());
    assert(ids.size() >= 7u);
}

void test_integrator_alias_resolution() {
    auto& registry = TimeIntegratorRegistry::instance();

    const auto euler = registry.get("Euler Explicit");
    const auto rk2 = registry.get("RK2");
    const auto rk3 = registry.get("RK3 Heun");
    const auto semiImplicit = registry.get("semi-implicit euler");
    const auto verlet = registry.get("Verlet");
    const auto crank = registry.get("Crank-Nicolson");
    const auto rk4 = registry.get("RK4");

    assert(euler != nullptr);
    assert(rk2 != nullptr);
    assert(rk3 != nullptr);
    assert(semiImplicit != nullptr);
    assert(verlet != nullptr);
    assert(crank != nullptr);
    assert(rk4 != nullptr);

    assert(euler->name() == "Euler Explicit");
    assert(rk2->name() == "RK2 Midpoint");
    assert(rk3->name() == "RK3 Heun");
    assert(semiImplicit->name() == "Semi-Implicit Euler");
    assert(verlet->name() == "Velocity Verlet");
    assert(crank->name() == "Crank-Nicolson");
    assert(rk4->name() == "RK4");
}

void test_integrator_step_contract() {
    StateBuffer state;
    state.data = {1.0f, 2.0f, 3.0f};
    const std::vector<float> derivatives = {0.5f, -1.0f, 2.0f};

    auto& registry = TimeIntegratorRegistry::instance();
    const auto rk2 = registry.get("rk2_midpoint");
    const auto verlet = registry.get("velocity_verlet");
    const auto crank = registry.get("crank_nicolson");

    assert(rk2 != nullptr);
    assert(verlet != nullptr);
    assert(crank != nullptr);

    StateBuffer rk2State = state;
    rk2->step(rk2State, derivatives, 0.5f);
    assert(rk2State.data[0] == 1.25f);
    assert(rk2State.data[1] == 1.5f);
    assert(rk2State.data[2] == 4.0f);

    StateBuffer verletState = state;
    verlet->step(verletState, derivatives, 0.25f);
    assert(verletState.data[0] == 1.125f);
    assert(verletState.data[1] == 1.75f);
    assert(verletState.data[2] == 3.5f);

    StateBuffer crankState = state;
    crank->step(crankState, derivatives, 1.0f);
    assert(crankState.data[0] == 1.5f);
    assert(crankState.data[1] == 1.0f);
    assert(crankState.data[2] == 5.0f);
}

int main() {
    test_integrator_registry_catalog();
    test_integrator_alias_resolution();
    test_integrator_step_contract();
    std::cout << "Time integrator registry tests passed.\n";
    return 0;
}