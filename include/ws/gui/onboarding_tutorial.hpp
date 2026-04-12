#pragma once

#include "ws/gui/main_window/app_state.hpp"

#include <array>
#include <cstddef>

namespace ws::gui {

struct OnboardingTutorialStep {
    const char* id;
    const char* title;
    const char* objective;
    const char* guidance;
    main_window::AppState recommendedState;
};

class OnboardingTutorial {
public:
    static constexpr std::array<OnboardingTutorialStep, 6> kSteps = {{
        {"welcome", "Welcome", "Understand workflow stages",
            "World-Simulator uses a staged workflow: select a model, create/open a world, run, then analyze and persist outcomes.",
            main_window::AppState::ModelSelector},
        {"select_model", "Select a model", "Choose simulation package scope",
            "Open Model Selector and pick a model package that matches your experiment goals.",
            main_window::AppState::ModelSelector},
        {"prepare_world", "Prepare a world", "Create or open world state",
            "Use Session Manager to open an existing world or launch the New World Wizard with deterministic seed and grid settings.",
            main_window::AppState::SessionManager},
        {"wizard", "Configure wizard", "Finalize world initialization",
            "In New World Wizard, review bindings and preflight checks before creating the world to avoid runtime mismatches.",
            main_window::AppState::NewWorldWizard},
        {"runtime", "Run and interact", "Control runtime and interventions",
            "In Simulation view, use play/pause, stepping, checkpoints, and interventions while preserving deterministic workflow order.",
            main_window::AppState::Simulation},
        {"persist", "Persist and resume", "Save world and checkpoints",
            "Save Active World and named checkpoints after meaningful interventions so replay, comparison, and resume workflows remain reproducible.",
            main_window::AppState::Simulation},
    }};

    [[nodiscard]] std::size_t stepCount() const { return kSteps.size(); }
    [[nodiscard]] std::size_t index() const { return index_; }
    [[nodiscard]] const OnboardingTutorialStep& current() const { return kSteps[index_]; }
    [[nodiscard]] bool atFirst() const { return index_ == 0u; }
    [[nodiscard]] bool atLast() const { return index_ + 1u >= kSteps.size(); }
    [[nodiscard]] bool completed() const { return completed_; }

    void reset() {
        index_ = 0u;
        completed_ = false;
    }

    void advance() {
        if (index_ + 1u < kSteps.size()) {
            ++index_;
            return;
        }
        completed_ = true;
    }

    void back() {
        if (index_ == 0u) {
            return;
        }
        --index_;
    }

private:
    std::size_t index_ = 0u;
    bool completed_ = false;
};

} // namespace ws::gui
