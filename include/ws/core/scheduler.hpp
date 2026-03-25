#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/state_store.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ws {

class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<std::string> declaredWriteSet() const = 0;
    virtual void initialize(StateStore::WriteSession& writeSession, const ModelProfile& profile) = 0;
    virtual void step(StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) = 0;
};

class Scheduler {
public:
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    void initialize(StateStore& stateStore, const ModelProfile& profile);
    void step(StateStore& stateStore, const ModelProfile& profile, std::uint64_t stepIndex);

    [[nodiscard]] std::vector<std::string> activeSubsystemNames() const;

private:
    std::vector<std::shared_ptr<ISubsystem>> subsystems_;
};

} // namespace ws
