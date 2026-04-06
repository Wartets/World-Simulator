#pragma once

namespace ws::app {

// =============================================================================
// Runtime Shell
// =============================================================================

// Command-line shell interface for the simulation runtime.
// Provides an interactive terminal for running simulations.
class RuntimeShell {
public:
    // Runs the shell interface. Returns exit code on termination.
    int run();
};

} // namespace ws::app
