#include "ws/gui/main_window.hpp"
#include "ws/gui/exception_message.hpp"

#include <exception>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

// GUI application entry point.
// Creates and runs the main window with exception handling.
int GuiMain() {
    try {
        ws::gui::MainWindow window;
        return window.run();
    } catch (const std::exception& exception) {
        const auto translated = ws::gui::translateExceptionMessage(
            exception,
            "GUI startup failed",
            "Check the configuration, model files, and available system resources, then retry.");
        std::cerr << translated.userMessage << '\n'
                  << translated.technicalDetail << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "What happened: GUI startup failed | Why: an unknown exception was raised | Next: Check the configuration, model files, and available system resources, then retry.\n"
                  << "GUI startup failed | unknown exception" << std::endl;
        return 1;
    }
}

// Standard console entry point.
int main() {
    return GuiMain();
}

#ifdef _WIN32
// Windows GUI entry point (WinMain).
// Allows application to be launched without console window.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return GuiMain();
}
#endif
