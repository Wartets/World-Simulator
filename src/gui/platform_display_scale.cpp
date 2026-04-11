#include "ws/gui/platform_display_scale.hpp"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace ws::gui::platform {
namespace {

float clampScale(const float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(scale, 0.75f, 3.0f);
}

#ifdef _WIN32
float computeWin32Scale(GLFWwindow* window) {
    if (window == nullptr) {
        return 0.0f;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return 0.0f;
    }

    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const auto getDpiForWindowFn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindowFn != nullptr) {
            const UINT dpi = getDpiForWindowFn(hwnd);
            if (dpi > 0u) {
                return static_cast<float>(dpi) / 96.0f;
            }
        }
    }

    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return 0.0f;
    }
    const int logPixelsX = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);

    if (logPixelsX > 0) {
        return static_cast<float>(logPixelsX) / 96.0f;
    }
    return 0.0f;
}
#endif

} // anonymous namespace

float computeEffectiveDisplayScale(GLFWwindow* window, const float fallbackScale) {
    float xScale = 0.0f;
    float yScale = 0.0f;

    if (window != nullptr) {
        glfwGetWindowContentScale(window, &xScale, &yScale);
    }

    float scale = std::max(xScale, yScale);

#ifdef _WIN32
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = computeWin32Scale(window);
    }
#endif

    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = fallbackScale;
    }

    return clampScale(scale);
}

} // namespace ws::gui::platform
