#pragma once

#include "ws/core/scheduler.hpp"

#include <memory>
#include <vector>

namespace ws {

// =============================================================================
// Cellular Automaton Subsystem
// =============================================================================

// Implements cellular automaton rules (e.g., Conway's Game of Life).
class CellularAutomatonSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Forest Fire Subsystem
// =============================================================================

// Simulates forest fire spread dynamics.
class ForestFireSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Generation Subsystem
// =============================================================================

// Handles procedural terrain and initial condition generation.
class GenerationSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Hydrology Subsystem
// =============================================================================

// Simulates water flow, accumulation, and evaporation.
class HydrologySubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Temperature Subsystem
// =============================================================================

// Simulates temperature dynamics with solar input and diffusion.
class TemperatureSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Humidity Subsystem
// =============================================================================

// Simulates humidity and moisture dynamics.
class HumiditySubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Wind Subsystem
// =============================================================================

// Simulates wind field dynamics.
class WindSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Climate Subsystem
// =============================================================================

// Coordinates climate interactions across subsystems.
class ClimateSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Soil Subsystem
// =============================================================================

// Simulates soil moisture and resource dynamics.
class SoilSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Vegetation Subsystem
// =============================================================================

// Simulates vegetation growth and dynamics.
class VegetationSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Resources Subsystem
// =============================================================================

// Manages resource distribution and consumption.
class ResourcesSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// =============================================================================
// Event Subsystem
// =============================================================================

// Processes and applies runtime events to the simulation.
class EventSubsystem final : public ISubsystem {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::vector<std::string> declaredReadSet() const override;
    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override;
    void initialize(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile) override;
    void step(const StateStore& stateStore, StateStore::WriteSession& writeSession, const ModelProfile& profile, std::uint64_t stepIndex) override;
};

// Creates a vector of Phase 4 subsystems for simulation.
[[nodiscard]] std::vector<std::shared_ptr<ISubsystem>> makePhase4Subsystems();

} // namespace ws
