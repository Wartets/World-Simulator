#include "ws/app/runtime_shell.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        ws::app::RuntimeShell shell;
        return shell.run();
    } catch (const std::exception& exception) {
        std::cerr << "runtime_error=" << exception.what() << '\n';
        return 1;
    }
}
