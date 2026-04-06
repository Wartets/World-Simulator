#include "ws/app/runtime_shell.hpp"

#include <exception>
#include <iostream>

// Application entry point for command-line runtime.
// Initializes the runtime shell and executes user commands.
int main() {
    try {
        ws::app::RuntimeShell shell;
        return shell.run();
    } catch (const std::exception& exception) {
        std::cerr << "runtime_error=" << exception.what() << '\n';
        return 1;
    }
}
