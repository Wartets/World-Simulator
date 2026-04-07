#include "ws/gui/viewport_manager.hpp"

#include <algorithm>

namespace ws::gui {

namespace {

// Applies pan offset to all cameras in vector.
// @param cameras Vector of viewport cameras
// @param panX New pan X offset
// @param panY New pan Y offset
void applyPanToAll(std::vector<ViewportCamera>& cameras, const float panX, const float panY) {
    for (auto& camera : cameras) {
        camera.panX = panX;
        camera.panY = panY;
    }
}

// Applies zoom level to all cameras in vector.
// @param cameras Vector of viewport cameras
// @param zoom New zoom level
void applyZoomToAll(std::vector<ViewportCamera>& cameras, const float zoom) {
    for (auto& camera : cameras) {
        camera.zoom = zoom;
    }
}

} // namespace

// Constructs viewport manager with specified number of viewports.
// @param count Number of viewports to manage
ViewportManager::ViewportManager(const std::size_t count)
    : cameras_(count), screenshotRequests_(count) {}

// Resizes viewport manager to new count.
// Preserves camera state for existing indices, initializes new ones.
// @param count New number of viewports
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

// Enables or disables synchronized panning across all viewports.
// When enabled, all cameras share the same pan offset.
// @param enabled true to enable sync, false to disable
void ViewportManager::setSyncPan(const bool enabled) {
    syncPan_ = enabled;
    if (syncPan_ && !cameras_.empty()) {
        applyPanToAll(cameras_, cameras_.front().panX, cameras_.front().panY);
    }
}

// Enables or disables synchronized zooming across all viewports.
// When enabled, all cameras share the same zoom level.
// @param enabled true to enable sync, false to disable
void ViewportManager::setSyncZoom(const bool enabled) {
    syncZoom_ = enabled;
    if (syncZoom_ && !cameras_.empty()) {
        applyZoomToAll(cameras_, cameras_.front().zoom);
    }
}

// Sets pan offset for a specific viewport or all if sync enabled.
// @param viewportIndex Index of viewport to modify
// @param panX New pan X offset
// @param panY New pan Y offset
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

// Sets zoom level for a specific viewport or all if sync enabled.
// Zoom is clamped to range [0.05, 24.0].
// @param viewportIndex Index of viewport to modify
// @param zoom New zoom level
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

// Resets viewport camera to default position (zoom 1.0, pan 0,0).
// @param viewportIndex Index of viewport to reset
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

// Gets camera state for viewport.
// @param viewportIndex Index of viewport
// @return Const reference to camera, or default camera if index out of range
const ViewportCamera& ViewportManager::camera(const std::size_t viewportIndex) const {
    static const ViewportCamera kDefault{};
    if (viewportIndex >= cameras_.size()) {
        return kDefault;
    }
    return cameras_[viewportIndex];
}

// Requests screenshot capture for viewport.
// @param viewportIndex Index of viewport to capture
// @param outputPath Path where screenshot should be saved
void ViewportManager::requestScreenshot(const std::size_t viewportIndex, std::string outputPath) {
    if (viewportIndex >= screenshotRequests_.size()) {
        return;
    }
    screenshotRequests_[viewportIndex].pending = true;
    screenshotRequests_[viewportIndex].outputPath = std::move(outputPath);
}

// Consumes and returns pending screenshot request for viewport.
// Clears the request after returning.
// @param viewportIndex Index of viewport
// @return Screenshot request with output path and pending flag
ViewportSnapshotRequest ViewportManager::consumeScreenshotRequest(const std::size_t viewportIndex) {
    if (viewportIndex >= screenshotRequests_.size()) {
        return {};
    }

    ViewportSnapshotRequest req = screenshotRequests_[viewportIndex];
    screenshotRequests_[viewportIndex] = {};
    return req;
}

} // namespace ws::gui
