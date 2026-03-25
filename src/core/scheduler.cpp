#include "ws/core/scheduler.hpp"

#include <algorithm>
#include <stdexcept>

namespace ws {

void Scheduler::registerSubsystem(std::shared_ptr<ISubsystem> subsystem) {
    if (!subsystem) {
        throw std::invalid_argument("Cannot register null subsystem");
    }
    subsystems_.push_back(std::move(subsystem));
    std::stable_sort(
        subsystems_.begin(),
        subsystems_.end(),
        [](const std::shared_ptr<ISubsystem>& left, const std::shared_ptr<ISubsystem>& right) {
            return left->name() < right->name();
        });
}

void Scheduler::initialize(StateStore& stateStore, const ModelProfile& profile) {
    for (const auto& subsystem : subsystems_) {
        StateStore::WriteSession session(stateStore, subsystem->name(), subsystem->declaredWriteSet());
        subsystem->initialize(session, profile);
    }
}

void Scheduler::step(StateStore& stateStore, const ModelProfile& profile, const std::uint64_t stepIndex) {
    for (const auto& subsystem : subsystems_) {
        StateStore::WriteSession session(stateStore, subsystem->name(), subsystem->declaredWriteSet());
        subsystem->step(session, profile, stepIndex);
    }
}

std::vector<std::string> Scheduler::activeSubsystemNames() const {
    std::vector<std::string> names;
    names.reserve(subsystems_.size());
    for (const auto& subsystem : subsystems_) {
        names.push_back(subsystem->name());
    }
    return names;
}

} // namespace ws
