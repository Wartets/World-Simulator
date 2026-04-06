#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <utility>

namespace ws {

// =============================================================================
// Runtime Control Surface
// =============================================================================

// Provides a controlled interface to the runtime for external controllers.
// Wraps runtime operations with a simplified API.
class RuntimeControlSurface {
public:
    // Constructs a control surface bound to the given runtime.
    explicit RuntimeControlSurface(Runtime& runtime) noexcept : runtime_(runtime) {}

    // Selects an execution profile for the runtime.
    void selectProfile(ProfileResolverInput profileInput) { runtime_.selectProfile(std::move(profileInput)); }
    // Updates the numerical guardrail policy.
    void updateGuardrails(NumericGuardrailPolicy guardrailPolicy) { runtime_.updateGuardrailPolicy(std::move(guardrailPolicy)); }
    // Pauses the simulation.
    void pause() { runtime_.pause(); }
    // Resumes a paused simulation.
    void resume() { runtime_.resume(); }
    // Executes a specified number of steps.
    void step(std::uint32_t count) { runtime_.controlledStep(count); }
    // Resets the runtime to a checkpoint.
    void reset(const RuntimeCheckpoint& checkpoint) { runtime_.resetToCheckpoint(checkpoint); }
    // Queues an input frame for processing.
    void queueInput(RuntimeInputFrame inputFrame) { runtime_.queueInput(std::move(inputFrame)); }
    // Enqueues a runtime event.
    void enqueueEvent(RuntimeEvent event) { runtime_.enqueueEvent(std::move(event)); }

private:
    Runtime& runtime_;
};

} // namespace ws
