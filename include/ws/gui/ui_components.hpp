#pragma once

#include <imgui.h>

#include <string>

namespace ws::gui {

bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 32));
bool SecondaryButton(const char* label, ImVec2 size = ImVec2(0, 32));
bool NumericSliderPair(const char* label, float* value, float minValue, float maxValue, const char* format = "%.3f", float inputWidth = 70.0f);
bool NumericSliderPairInt(const char* label, int* value, int minValue, int maxValue, const char* format = "%d", float inputWidth = 55.0f);
void DelayedTooltip(const char* text);

void SectionHeader(const char* title, const char* subtitle = nullptr);
void EmptyStateCard(const char* title, const char* body);
void LabeledHint(const char* text);

void PushSectionTint(int index);
void PopSectionTint();

void StatusBadge(const char* label, bool active);

} // namespace ws::gui
