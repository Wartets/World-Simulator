#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ws::gui {

// Blend mode for paint operations.
enum class PaintBlendMode : std::uint8_t {
    Set = 0,       // Replace existing values
    Add = 1,       // Add to existing values
    Subtract = 2,  // Subtract from existing values
    Multiply = 3   // Multiply with existing values
};

// Configuration for a paint session.
struct PaintSessionConfig {
    std::size_t width = 0;
    std::size_t height = 0;
    float domainMin = 0.0f;
    float domainMax = 1.0f;
};

// Tools for painting directly on simulation fields.
// Supports brush, smooth, fill, eyedropper, and eraser operations
// with undo/redo capability.
class PaintTools {
public:
    explicit PaintTools(PaintSessionConfig config = {});

    // Resets the paint session with new configuration and initial values.
    void reset(PaintSessionConfig config, std::vector<float> initialValues);

    // Begins a new painting stroke, recording baseline values.
    void beginStroke(const std::vector<float>& currentValues);
    // Ends the current stroke, saving to undo stack.
    bool endStroke(const std::vector<float>& currentValues);

    // Undo last paint operation.
    bool undo(std::vector<float>& values);
    // Redo previously undone operation.
    bool redo(std::vector<float>& values);

    // Apply brush stroke at position.
    void brush(
        std::vector<float>& values,
        int centerX,
        int centerY,
        float radius,
        float strength,
        float value,
        PaintBlendMode mode);

    // Smooth values around a position.
    void smooth(std::vector<float>& values, int centerX, int centerY, float radius, float strength);
    // Flood fill starting from a position.
    void fill(std::vector<float>& values, int startX, int startY, float targetTolerance, float replacementValue);
    // Sample value at position.
    bool eyedropper(const std::vector<float>& values, int x, int y, float& outValue) const;
    // Erase (set to zero) at position.
    void erase(std::vector<float>& values, int x, int y);

private:
    // Check if coordinates are within grid bounds.
    [[nodiscard]] bool inBounds(int x, int y) const;
    // Convert 2D coordinates to linear index.
    [[nodiscard]] std::size_t indexOf(int x, int y) const;
    // Clamp value to configured domain.
    [[nodiscard]] float clampToDomain(float value) const;

    PaintSessionConfig config_{};
    std::vector<float> initialValues_;
    std::vector<float> strokeBaseline_;
    std::vector<std::vector<float>> undoStack_;
    std::vector<std::vector<float>> redoStack_;
    bool strokeActive_ = false;
};

} // namespace ws::gui
