#pragma once

#include "ws/core/runtime.hpp"
#include "ws/core/state_store.hpp"

namespace ws::initialization {

// Applies non-terrain initial condition generators to the state store.
void applyNonTerrainInitialization(StateStore& stateStore, const RuntimeConfig& config);

} // namespace ws::initialization
