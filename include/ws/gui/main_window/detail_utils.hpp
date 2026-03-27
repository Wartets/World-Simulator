#pragma once

#include "ws/core/state_store.hpp"
#include "ws/gui/main_window/panel_state.hpp"

#include <cstdint>
#include <vector>

#include <imgui.h>

namespace ws::gui::main_window::detail {

std::vector<float> mergedFieldValues(const StateStoreSnapshot::FieldPayload& field, bool includeSparseOverlay);
void minMaxFinite(const std::vector<float>& values, float& outMin, float& outMax);

std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value);
std::uint64_t hashFloat(float value);
void unpackColor(ImU32 color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a);

float previewTerrainValue(const PanelState& panel, int x, int y, int w, int h);

} // namespace ws::gui::main_window::detail
