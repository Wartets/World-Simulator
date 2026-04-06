#include "ws/gui/viewport_manager.hpp"

#include <cassert>
#include <cmath>

int main() {
    ws::gui::ViewportManager manager(2);
    assert(manager.count() == 2);

    manager.setPan(0, 12.0f, -7.0f);
    manager.setZoom(0, 1.5f);
    manager.setPan(1, -3.0f, 4.0f);
    manager.setZoom(1, 2.25f);

    manager.setSyncPan(true);
    manager.setSyncZoom(true);

    const auto& cam0 = manager.camera(0);
    const auto& cam1 = manager.camera(1);
    assert(std::fabs(cam0.panX - cam1.panX) < 1e-6f);
    assert(std::fabs(cam0.panY - cam1.panY) < 1e-6f);
    assert(std::fabs(cam0.zoom - cam1.zoom) < 1e-6f);

    manager.resize(4);
    assert(manager.count() == 4);
    for (std::size_t i = 0; i < manager.count(); ++i) {
        const auto& cam = manager.camera(i);
        assert(std::fabs(cam.panX - cam0.panX) < 1e-6f);
        assert(std::fabs(cam.panY - cam0.panY) < 1e-6f);
        assert(std::fabs(cam.zoom - cam0.zoom) < 1e-6f);
    }

    manager.setSyncPan(false);
    manager.setSyncZoom(false);
    manager.setPan(3, 8.0f, 9.0f);
    manager.setZoom(3, 3.0f);
    assert(std::fabs(manager.camera(3).panX - 8.0f) < 1e-6f);
    assert(std::fabs(manager.camera(3).panY - 9.0f) < 1e-6f);
    assert(std::fabs(manager.camera(3).zoom - 3.0f) < 1e-6f);
    assert(std::fabs(manager.camera(0).panX - manager.camera(1).panX) < 1e-6f);

    manager.resize(1);
    assert(manager.count() == 1);
    const auto& single = manager.camera(0);
    assert(std::isfinite(single.zoom));
    assert(std::isfinite(single.panX));
    assert(std::isfinite(single.panY));

    return 0;
}
