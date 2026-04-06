#pragma once

#include <imgui.h>

#include <string>

namespace ws::gui {

// Creates a primary styled button.
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 32));
// Creates a secondary styled button.
bool SecondaryButton(const char* label, ImVec2 size = ImVec2(0, 32));
// Creates a slider with paired numeric input for a float value.
bool NumericSliderPair(const char* label, float* value, float minValue, float maxValue, const char* format = "%.3f", float inputWidth = 70.0f);
// Creates a slider with paired numeric input for an int value.
bool NumericSliderPairInt(const char* label, int* value, int minValue, int maxValue, const char* format = "%d", float inputWidth = 55.0f);
// Shows a tooltip after a delay.
void DelayedTooltip(const char* text);

// Renders a section header with optional subtitle.
void SectionHeader(const char* title, const char* subtitle = nullptr);
// Renders an empty state placeholder card.
void EmptyStateCard(const char* title, const char* body);
// Renders a labeled hint text.
void LabeledHint(const char* text);

// Pushes a section tint color based on index.
void PushSectionTint(int index);
// Pops the current section tint.
void PopSectionTint();

// Renders a status badge.
void StatusBadge(const char* label, bool active);

} // namespace ws::gui
