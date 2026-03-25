#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <utility>

namespace ws {

class RuntimeControlSurface {
public:
    explicit RuntimeControlSurface(Runtime& runtime) noexcept : runtime_(runtime) {}

    void selectProfile(ProfileResolverInput profileInput) { runtime_.selectProfile(std::move(profileInput)); }
    void updateGuardrails(NumericGuardrailPolicy guardrailPolicy) { runtime_.updateGuardrailPolicy(std::move(guardrailPolicy)); }
    void pause() { runtime_.pause(); }
    void resume() { runtime_.resume(); }
    void step(std::uint32_t count) { runtime_.controlledStep(count); }
    void reset(const RuntimeCheckpoint& checkpoint) { runtime_.resetToCheckpoint(checkpoint); }
    void queueInput(RuntimeInputFrame inputFrame) { runtime_.queueInput(std::move(inputFrame)); }
    void enqueueEvent(RuntimeEvent event) { runtime_.enqueueEvent(std::move(event)); }

private:
    Runtime& runtime_;
};

} // namespace ws
