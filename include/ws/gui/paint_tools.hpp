#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ws::gui {

enum class PaintBlendMode : std::uint8_t {
    Set = 0,
    Add = 1,
    Subtract = 2,
    Multiply = 3,
};

struct PaintSessionConfig {
    std::size_t width = 0;
    std::size_t height = 0;
    float domainMin = 0.0f;
    float domainMax = 1.0f;
};

class PaintTools {
public:
    explicit PaintTools(PaintSessionConfig config = {});

    void reset(PaintSessionConfig config, std::vector<float> initialValues);

    void beginStroke(const std::vector<float>& currentValues);
    bool endStroke(const std::vector<float>& currentValues);

    bool undo(std::vector<float>& values);
    bool redo(std::vector<float>& values);

    void brush(
        std::vector<float>& values,
        int centerX,
        int centerY,
        float radius,
        float strength,
        float value,
        PaintBlendMode mode);

    void smooth(std::vector<float>& values, int centerX, int centerY, float radius, float strength);
    void fill(std::vector<float>& values, int startX, int startY, float targetTolerance, float replacementValue);
    bool eyedropper(const std::vector<float>& values, int x, int y, float& outValue) const;
    void erase(std::vector<float>& values, int x, int y);

private:
    [[nodiscard]] bool inBounds(int x, int y) const;
    [[nodiscard]] std::size_t indexOf(int x, int y) const;
    [[nodiscard]] float clampToDomain(float value) const;

    PaintSessionConfig config_{};
    std::vector<float> initialValues_;
    std::vector<float> strokeBaseline_;
    std::vector<std::vector<float>> undoStack_;
    std::vector<std::vector<float>> redoStack_;
    bool strokeActive_ = false;
};

} // namespace ws::gui
