#pragma once

#include <filesystem>

struct GLFWwindow;

namespace ws::gui::main_window {

struct PersistedWindowState {
    int x = 100;
    int y = 100;
    int width = 1280;
    int height = 720;
    bool maximized = false;
    int monitorIndex = 0;
    bool loaded = false;
};

[[nodiscard]] std::filesystem::path resolveWorkspaceRoot();
[[nodiscard]] std::filesystem::path windowStatePath();

bool loadWindowState(PersistedWindowState& state);
void applyWindowState(GLFWwindow* window, PersistedWindowState state);
void saveWindowState(GLFWwindow* window);

} // namespace ws::gui::main_window
