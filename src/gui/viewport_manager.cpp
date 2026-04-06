#include "ws/gui/viewport_manager.hpp"

#include <algorithm>

namespace ws::gui {

namespace {

void applyPanToAll(std::vector<ViewportCamera>& cameras, const float panX, const float panY) {
    for (auto& camera : cameras) {
        camera.panX = panX;
        camera.panY = panY;
    }
}

void applyZoomToAll(std::vector<ViewportCamera>& cameras, const float zoom) {
    for (auto& camera : cameras) {
        camera.zoom = zoom;
    }
}

} // namespace

ViewportManager::ViewportManager(const std::size_t count)
    : cameras_(count), screenshotRequests_(count) {}

void ViewportManager::resize(const std::size_t count) {
    if (count == cameras_.size()) {
        return;
    }

    const ViewportCamera reference = cameras_.empty() ? ViewportCamera{} : cameras_.front();
    cameras_.resize(count);
    screenshotRequests_.resize(count);

    if (cameras_.empty()) {
        return;
    }

    if (syncPan_) {
        applyPanToAll(cameras_, reference.panX, reference.panY);
    }
    if (syncZoom_) {
        applyZoomToAll(cameras_, reference.zoom);
    }
}

void ViewportManager::setSyncPan(const bool enabled) {
    syncPan_ = enabled;
    if (syncPan_ && !cameras_.empty()) {
        applyPanToAll(cameras_, cameras_.front().panX, cameras_.front().panY);
    }
}

void ViewportManager::setSyncZoom(const bool enabled) {
    syncZoom_ = enabled;
    if (syncZoom_ && !cameras_.empty()) {
        applyZoomToAll(cameras_, cameras_.front().zoom);
    }
}

void ViewportManager::setPan(const std::size_t viewportIndex, const float panX, const float panY) {
    if (viewportIndex >= cameras_.size()) {
        return;
    }

    if (syncPan_) {
        for (auto& camera : cameras_) {
            camera.panX = panX;
            camera.panY = panY;
        }
        return;
    }

    cameras_[viewportIndex].panX = panX;
    cameras_[viewportIndex].panY = panY;
}

void ViewportManager::setZoom(const std::size_t viewportIndex, const float zoom) {
    if (viewportIndex >= cameras_.size()) {
        return;
    }

    const float clamped = std::clamp(zoom, 0.05f, 24.0f);
    if (syncZoom_) {
        for (auto& camera : cameras_) {
            camera.zoom = clamped;
        }
        return;
    }

    cameras_[viewportIndex].zoom = clamped;
}

void ViewportManager::fit(const std::size_t viewportIndex) {
    if (viewportIndex >= cameras_.size()) {
        return;
    }

    if (syncPan_ || syncZoom_) {
        for (auto& camera : cameras_) {
            camera.zoom = 1.0f;
            camera.panX = 0.0f;
            camera.panY = 0.0f;
        }
        return;
    }

    cameras_[viewportIndex].zoom = 1.0f;
    cameras_[viewportIndex].panX = 0.0f;
    cameras_[viewportIndex].panY = 0.0f;
}

const ViewportCamera& ViewportManager::camera(const std::size_t viewportIndex) const {
    static const ViewportCamera kDefault{};
    if (viewportIndex >= cameras_.size()) {
        return kDefault;
    }
    return cameras_[viewportIndex];
}

void ViewportManager::requestScreenshot(const std::size_t viewportIndex, std::string outputPath) {
    if (viewportIndex >= screenshotRequests_.size()) {
        return;
    }
    screenshotRequests_[viewportIndex].pending = true;
    screenshotRequests_[viewportIndex].outputPath = std::move(outputPath);
}

ViewportSnapshotRequest ViewportManager::consumeScreenshotRequest(const std::size_t viewportIndex) {
    if (viewportIndex >= screenshotRequests_.size()) {
        return {};
    }

    ViewportSnapshotRequest req = screenshotRequests_[viewportIndex];
    screenshotRequests_[viewportIndex] = {};
    return req;
}

} // namespace ws::gui
