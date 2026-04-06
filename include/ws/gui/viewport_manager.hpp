#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ws::gui {

enum class ViewportRenderMode {
    Heatmap = 0,
    Vector,
    Contour,
    CustomRule
};

struct ViewportCamera {
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
};

struct ViewportSnapshotRequest {
    bool pending = false;
    std::string outputPath;
};

class ViewportManager {
public:
    explicit ViewportManager(std::size_t count = 4);

    void resize(std::size_t count);
    [[nodiscard]] std::size_t count() const { return cameras_.size(); }

    void setSyncPan(bool enabled);
    void setSyncZoom(bool enabled);

    [[nodiscard]] bool syncPan() const { return syncPan_; }
    [[nodiscard]] bool syncZoom() const { return syncZoom_; }

    void setPan(std::size_t viewportIndex, float panX, float panY);
    void setZoom(std::size_t viewportIndex, float zoom);
    void fit(std::size_t viewportIndex);

    [[nodiscard]] const ViewportCamera& camera(std::size_t viewportIndex) const;

    void requestScreenshot(std::size_t viewportIndex, std::string outputPath);
    [[nodiscard]] ViewportSnapshotRequest consumeScreenshotRequest(std::size_t viewportIndex);

private:
    std::vector<ViewportCamera> cameras_;
    std::vector<ViewportSnapshotRequest> screenshotRequests_;
    bool syncPan_ = false;
    bool syncZoom_ = false;
};

} // namespace ws::gui
