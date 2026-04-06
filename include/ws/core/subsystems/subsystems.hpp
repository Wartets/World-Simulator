#pragma once

#include "ws/core/scheduler.hpp"

#include <memory>
#include <vector>

namespace ws {

class CellularAutomatonSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class GenerationSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class HydrologySubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class TemperatureSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class HumiditySubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class WindSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class ClimateSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class SoilSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class VegetationSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class ResourcesSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

class EventSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

[[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> makePhase4Subsystems();

} // namespace ws
