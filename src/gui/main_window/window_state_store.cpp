#include "ws/gui/main_window/window_state_store.hpp"
#include "ws/gui/storage_paths.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <fstream>
#include <string>

namespace ws::gui::main_window {

namespace {

void clampToMonitorWorkarea(
    int& x,
    int& y,
    int& width,
    int& height,
    const int monitorX,
    const int monitorY,
    const int monitorW,
    const int monitorH) {
    width = std::clamp(width, 640, std::max(640, monitorW));
    height = std::clamp(height, 480, std::max(480, monitorH));
    x = std::clamp(x, monitorX, monitorX + std::max(0, monitorW - width));
    y = std::clamp(y, monitorY, monitorY + std::max(0, monitorH - height));
}

} // namespace

std::filesystem::path resolveWorkspaceRoot() {
    return storage::resolveWorkspaceRootFromCurrentPath();
}

std::filesystem::path windowStatePath() {
    const std::filesystem::path root = storage::resolveProfilesPath("WorldSimulator");
    return root / "window_state.cfg";
}

bool loadWindowState(PersistedWindowState& state) {
    std::ifstream in(windowStatePath());
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "x") state.x = std::stoi(value);
            else if (key == "y") state.y = std::stoi(value);
            else if (key == "width") state.width = std::stoi(value);
            else if (key == "height") state.height = std::stoi(value);
            else if (key == "maximized") state.maximized = (std::stoi(value) != 0);
            else if (key == "monitor_index") state.monitorIndex = std::stoi(value);
        } catch (...) {
        }
    }

    state.width = std::clamp(state.width, 640, 8192);
    state.height = std::clamp(state.height, 480, 8192);
    state.loaded = true;
    return true;
}

void applyWindowState(GLFWwindow* window, PersistedWindowState state) {
    if (window == nullptr || !state.loaded) {
        return;
    }

    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (monitors == nullptr || monitorCount <= 0) {
        return;
    }

    state.monitorIndex = std::clamp(state.monitorIndex, 0, monitorCount - 1);
    GLFWmonitor* monitor = monitors[state.monitorIndex];

    int monitorX = 0;
    int monitorY = 0;
    int monitorW = 0;
    int monitorH = 0;
    glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorW, &monitorH);
    if (monitorW <= 0 || monitorH <= 0) {
        monitor = glfwGetPrimaryMonitor();
        glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorW, &monitorH);
    }

    clampToMonitorWorkarea(state.x, state.y, state.width, state.height, monitorX, monitorY, monitorW, monitorH);

    glfwSetWindowSize(window, state.width, state.height);
    glfwSetWindowPos(window, state.x, state.y);
    if (state.maximized) {
        glfwMaximizeWindow(window);
    }
}

void saveWindowState(GLFWwindow* window) {
    if (window == nullptr) {
        return;
    }

    PersistedWindowState state;
    glfwGetWindowPos(window, &state.x, &state.y);
    glfwGetWindowSize(window, &state.width, &state.height);
    state.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;

    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    state.monitorIndex = 0;
    if (monitors != nullptr && monitorCount > 0) {
        const int centerX = state.x + state.width / 2;
        const int centerY = state.y + state.height / 2;
        for (int i = 0; i < monitorCount; ++i) {
            int mx = 0;
            int my = 0;
            int mw = 0;
            int mh = 0;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            if (centerX >= mx && centerX < mx + mw && centerY >= my && centerY < my + mh) {
                state.monitorIndex = i;
                break;
            }
        }
    }

    std::ofstream out(windowStatePath(), std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << "x=" << state.x << '\n';
    out << "y=" << state.y << '\n';
    out << "width=" << state.width << '\n';
    out << "height=" << state.height << '\n';
    out << "maximized=" << (state.maximized ? 1 : 0) << '\n';
    out << "monitor_index=" << state.monitorIndex << '\n';
}

} // namespace ws::gui::main_window
