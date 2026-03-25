#include "ws/gui/main_window.hpp"

#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

int GuiMain() {
    try {
        ws::gui::MainWindow window;
        return window.run();
    } catch (const std::exception&) {
        return 1;
    }
}

int main() {
    return GuiMain();
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return GuiMain();
}
#endif
