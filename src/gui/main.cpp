#include "ws/gui/main_window.hpp"

#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

// GUI application entry point.
// Creates and runs the main window with exception handling.
int GuiMain() {
    try {
        ws::gui::MainWindow window;
        return window.run();
    } catch (const std::exception&) {
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
