#pragma once

#include "ws/core/runtime.hpp"
#include "ws/core/state_store.hpp"

namespace ws::initialization {

void applyNonTerrainInitialization(StateStore& stateStore, const RuntimeConfig& config);

} // namespace ws::initialization
