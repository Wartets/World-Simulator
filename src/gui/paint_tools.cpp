#include "ws/gui/paint_tools.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace ws::gui {

// Constructs paint tools with session configuration.
// @param config Paint session configuration with grid dimensions and domain
PaintTools::PaintTools(PaintSessionConfig config) : config_(config) {}

// Resets paint tools with new configuration and initial values.
// Clears undo/redo stacks and stroke state.
// @param config New paint session configuration
// @param initialValues Initial field values
void PaintTools::reset(PaintSessionConfig config, std::vector<float> initialValues) {
    config_ = config;
    initialValues_ = std::move(initialValues);
    if (initialValues_.size() != config_.width * config_.height) {
        initialValues_.assign(config_.width * config_.height, config_.domainMin);
    }
    undoStack_.clear();
    redoStack_.clear();
    strokeBaseline_.clear();
    strokeActive_ = false;
}

// Begins paint stroke by capturing baseline values.
// @param currentValues Current field values at stroke start
void PaintTools::beginStroke(const std::vector<float>& currentValues) {
    if (strokeActive_) {
        return;
    }
    strokeBaseline_ = currentValues;
    strokeActive_ = true;
}

// Ends paint stroke and saves to undo stack if values changed.
// @param currentValues Current field values at stroke end
// @return true if stroke was saved to undo stack
bool PaintTools::endStroke(const std::vector<float>& currentValues) {
    if (!strokeActive_) {
        return false;
    }
    strokeActive_ = false;

    if (strokeBaseline_ != currentValues) {
        undoStack_.push_back(strokeBaseline_);
        if (undoStack_.size() > 128) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
        return true;
    }
    return false;
}

// Undoes last paint stroke by restoring from undo stack.
// @param values Field values to restore
// @return true if undo successful
bool PaintTools::undo(std::vector<float>& values) {
    if (undoStack_.empty()) {
        return false;
    }
    redoStack_.push_back(values);
    values = undoStack_.back();
    undoStack_.pop_back();
    return true;
}

// Redoes previously undone paint stroke.
// @param values Field values to restore
// @return true if redo successful
bool PaintTools::redo(std::vector<float>& values) {
    if (redoStack_.empty()) {
        return false;
    }
    undoStack_.push_back(values);
    values = redoStack_.back();
    redoStack_.pop_back();
    return true;
}

// Checks if coordinates are within grid bounds.
// @param x Grid X coordinate
// @param y Grid Y coordinate
// @return true if coordinates are valid
bool PaintTools::inBounds(const int x, const int y) const {
    return x >= 0 && y >= 0 &&
           static_cast<std::size_t>(x) < config_.width &&
           static_cast<std::size_t>(y) < config_.height;
}

std::size_t PaintTools::indexOf(const int x, const int y) const {
    return static_cast<std::size_t>(y) * config_.width + static_cast<std::size_t>(x);
}

float PaintTools::clampToDomain(const float value) const {
    return std::clamp(value, std::min(config_.domainMin, config_.domainMax), std::max(config_.domainMin, config_.domainMax));
}

void PaintTools::brush(
    std::vector<float>& values,
    const int centerX,
    const int centerY,
    const float radius,
    const float strength,
    const float value,
    const PaintBlendMode mode) {

    if (values.size() != config_.width * config_.height || radius <= 0.0f || strength <= 0.0f) {
        return;
    }

    const int r = static_cast<int>(std::ceil(radius));
    const float sigma = std::max(0.05f, radius * 0.45f);

    for (int y = centerY - r; y <= centerY + r; ++y) {
        for (int x = centerX - r; x <= centerX + r; ++x) {
            if (!inBounds(x, y)) {
                continue;
            }
            const float dx = static_cast<float>(x - centerX);
            const float dy = static_cast<float>(y - centerY);
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) {
                continue;
            }

            const float gaussian = std::exp(-(distance * distance) / (2.0f * sigma * sigma));
            const float alpha = std::clamp(gaussian * strength, 0.0f, 1.0f);
            const auto idx = indexOf(x, y);
            const float current = values[idx];

            float blended = current;
            switch (mode) {
                case PaintBlendMode::Add:
                    blended = current + (value * alpha);
                    break;
                case PaintBlendMode::Subtract:
                    blended = current - (value * alpha);
                    break;
                case PaintBlendMode::Multiply: {
                    const float factor = 1.0f + ((value - 1.0f) * alpha);
                    blended = current * factor;
                    break;
                }
                case PaintBlendMode::Set:
                default:
                    blended = current + ((value - current) * alpha);
                    break;
            }
            values[idx] = clampToDomain(blended);
        }
    }
}

void PaintTools::smooth(std::vector<float>& values, const int centerX, const int centerY, const float radius, const float strength) {
    if (values.size() != config_.width * config_.height || radius <= 0.0f || strength <= 0.0f) {
        return;
    }

    const int r = static_cast<int>(std::ceil(radius));
    std::vector<float> source = values;

    for (int y = centerY - r; y <= centerY + r; ++y) {
        for (int x = centerX - r; x <= centerX + r; ++x) {
            if (!inBounds(x, y)) {
                continue;
            }

            const float dx = static_cast<float>(x - centerX);
            const float dy = static_cast<float>(y - centerY);
            if (std::sqrt(dx * dx + dy * dy) > radius) {
                continue;
            }

            float sum = 0.0f;
            float weight = 0.0f;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (!inBounds(nx, ny)) {
                        continue;
                    }
                    const float w = (ox == 0 && oy == 0) ? 4.0f : ((ox == 0 || oy == 0) ? 2.0f : 1.0f);
                    sum += source[indexOf(nx, ny)] * w;
                    weight += w;
                }
            }

            if (weight <= 0.0f) {
                continue;
            }
            const auto idx = indexOf(x, y);
            const float average = sum / weight;
            const float alpha = std::clamp(strength, 0.0f, 1.0f);
            values[idx] = clampToDomain(source[idx] + ((average - source[idx]) * alpha));
        }
    }
}

void PaintTools::fill(std::vector<float>& values, const int startX, const int startY, const float targetTolerance, const float replacementValue) {
    if (values.size() != config_.width * config_.height || !inBounds(startX, startY)) {
        return;
    }

    const float sourceValue = values[indexOf(startX, startY)];
    const float newValue = clampToDomain(replacementValue);
    if (std::abs(sourceValue - newValue) <= 1e-6f) {
        return;
    }

    const float tolerance = std::max(0.0f, targetTolerance);
    std::vector<std::uint8_t> visited(config_.width * config_.height, 0u);
    std::queue<std::pair<int, int>> queue;
    queue.push({startX, startY});

    while (!queue.empty()) {
        const auto [x, y] = queue.front();
        queue.pop();

        if (!inBounds(x, y)) {
            continue;
        }

        const auto idx = indexOf(x, y);
        if (visited[idx] != 0u) {
            continue;
        }
        visited[idx] = 1u;

        if (std::abs(values[idx] - sourceValue) > tolerance) {
            continue;
        }

        values[idx] = newValue;

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) {
                    continue;
                }
                queue.push({x + ox, y + oy});
            }
        }
    }
}

bool PaintTools::eyedropper(const std::vector<float>& values, const int x, const int y, float& outValue) const {
    if (values.size() != config_.width * config_.height || !inBounds(x, y)) {
        return false;
    }
    outValue = values[indexOf(x, y)];
    return true;
}

void PaintTools::erase(std::vector<float>& values, const int x, const int y) {
    if (values.size() != config_.width * config_.height || !inBounds(x, y)) {
        return;
    }

    const auto idx = indexOf(x, y);
    if (idx < initialValues_.size()) {
        values[idx] = clampToDomain(initialValues_[idx]);
    } else {
        values[idx] = clampToDomain(config_.domainMin);
    }
}

} // namespace ws::gui
