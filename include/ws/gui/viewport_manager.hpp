#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Viewport Render Mode
// =============================================================================

// Rendering mode for viewports.
enum class ViewportRenderMode {
    Heatmap = 0,    // Scalar field heatmap visualization.
    Vector,        // Vector field arrow visualization.
    Contour,       // Contour line visualization.
    CustomRule     // Custom rule-based visualization.
};

// =============================================================================
// Viewport Camera
// =============================================================================

// Camera state for a viewport.
struct ViewportCamera {
    float zoom = 1.0f;         // Zoom level (1.0 = 100%).
    float panX = 0.0f;         // Horizontal pan offset.
    float panY = 0.0f;         // Vertical pan offset.
};

// =============================================================================
// Viewport Snapshot Request
// =============================================================================

// Request for capturing a viewport screenshot.
struct ViewportSnapshotRequest {
    bool pending = false;      // Whether a screenshot is pending.
    std::string outputPath;    // Output file path for the screenshot.
};

// =============================================================================
// Viewport Manager
// =============================================================================

// Manages multiple viewports with synchronized camera controls.
class ViewportManager {
public:
    // Constructs a manager with the specified number of viewports.
    explicit ViewportManager(std::size_t count = 4);

    // Resizes the viewport array.
    void resize(std::size_t count);
    // Returns the number of viewports.
    [[nodiscard]] std::size_t count() const { return cameras_.size(); }

    // Enables or disables synchronized pan across viewports.
    void setSyncPan(bool enabled);
    // Enables or disables synchronized zoom across viewports.
    void setSyncZoom(bool enabled);

    // Returns whether pan synchronization is enabled.
    [[nodiscard]] bool syncPan() const { return syncPan_; }
    // Returns whether zoom synchronization is enabled.
    [[nodiscard]] bool syncZoom() const { return syncZoom_; }

    // Sets pan offset for a specific viewport.
    void setPan(std::size_t viewportIndex, float panX, float panY);
    // Sets zoom level for a specific viewport.
    void setZoom(std::size_t viewportIndex, float zoom);
    // Fits the viewport to show the entire grid.
    void fit(std::size_t viewportIndex);

    // Returns the camera state for a viewport.
    [[nodiscard]] const ViewportCamera& camera(std::size_t viewportIndex) const;

    // Requests a screenshot for a viewport.
    void requestScreenshot(std::size_t viewportIndex, std::string outputPath);
    // Consumes and returns a pending screenshot request.
    [[nodiscard]] ViewportSnapshotRequest consumeScreenshotRequest(std::size_t viewportIndex);

private:
    std::vector<ViewportCamera> cameras_;
    std::vector<ViewportSnapshotRequest> screenshotRequests_;
    bool syncPan_ = false;
    bool syncZoom_ = false;
};

} // namespace ws::gui
