#include "ws/gui/onboarding_tutorial.hpp"

#include <cassert>

using ws::gui::OnboardingTutorial;
using ws::gui::main_window::AppState;

int main() {
    OnboardingTutorial tutorial;

    assert(tutorial.stepCount() == 6u);
    assert(tutorial.index() == 0u);
    assert(!tutorial.completed());
    assert(tutorial.current().recommendedState == AppState::ModelSelector);

    tutorial.advance();
    assert(tutorial.index() == 1u);
    tutorial.advance();
    assert(tutorial.index() == 2u);
    tutorial.back();
    assert(tutorial.index() == 1u);

    tutorial.reset();
    assert(tutorial.index() == 0u);
    assert(!tutorial.completed());

    for (std::size_t i = 0; i + 1u < tutorial.stepCount(); ++i) {
        tutorial.advance();
    }
    assert(tutorial.atLast());
    assert(!tutorial.completed());

    tutorial.advance();
    assert(tutorial.completed());

    tutorial.reset();
    assert(!tutorial.completed());
    assert(tutorial.index() == 0u);

    return 0;
}
