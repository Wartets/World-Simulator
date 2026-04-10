#include "ws/gui/display_manager.hpp"

#include <cassert>
#include <cmath>
#include <string>

using ws::StateStoreSnapshot;
using ws::VariableSpec;
using ws::gui::DisplayManagerParams;
using ws::gui::DisplayPreviewComponentsRequest;
using ws::gui::DisplaySnapshotRequest;
using ws::gui::DisplayTerrainPreviewRequest;
using ws::gui::DisplayType;
using ws::gui::buildDisplayBufferFromPreviewComponents;
using ws::gui::buildDisplayBufferFromSnapshot;
using ws::gui::buildDisplayBufferFromTerrain;
using ws::gui::emptyDisplayFieldTags;

int main() {
    {
        DisplayManagerParams params;
        params.autoWaterQuantile = 1.8f;
        params.waterLevel = -0.4f;
        params.lowlandThreshold = -1.0f;
        params.highlandThreshold = 5.0f;
        params.waterPresenceThreshold = -0.2f;
        params.shallowWaterDepth = 2.0f;
        params.highMoistureThreshold = 2.0f;

        const auto tuned = params.sanitized();
        assert(tuned.autoWaterQuantile >= 0.0f && tuned.autoWaterQuantile <= 1.0f);
        assert(tuned.waterLevel >= 0.0f && tuned.waterLevel <= 1.0f);
        assert(tuned.lowlandThreshold >= 0.0f && tuned.lowlandThreshold <= 1.0f);
        assert(tuned.highlandThreshold > tuned.lowlandThreshold);
        assert(tuned.waterPresenceThreshold >= 0.0f && tuned.waterPresenceThreshold <= 1.0f);
        assert(tuned.shallowWaterDepth >= 0.0f && tuned.shallowWaterDepth <= 0.5f);
        assert(tuned.highMoistureThreshold >= 0.0f && tuned.highMoistureThreshold <= 1.0f);
    }

    {
        DisplayManagerParams params;
        params.waterLevel = std::nanf("");
        std::string message;
        assert(!params.validate(message));
        assert(!message.empty());
    }

    StateStoreSnapshot snapshot;
    {
        StateStoreSnapshot::FieldPayload payload;
        payload.spec = VariableSpec{};
        payload.spec.name = "temperature";
        payload.values = {0.2f, 0.8f, 0.4f, 0.9f};
        payload.validityMask = {1u, 1u, 1u, 1u};
        snapshot.fields.push_back(payload);
    }

    {
        const DisplaySnapshotRequest invalidRequest{
            snapshot,
            9,
            DisplayType::ScalarField,
            false,
            std::cref(emptyDisplayFieldTags())};
        std::string message;
        assert(!invalidRequest.validate(message));

        const auto tunedRequest = invalidRequest.sanitized();
        assert(tunedRequest.primaryFieldIndex == 0);
        assert(tunedRequest.validate(message));

        DisplayManagerParams params;
        const auto buffer = buildDisplayBufferFromSnapshot(tunedRequest, params);
        assert(!buffer.values.empty());
    }

    {
        const std::vector<float> terrain = {0.1f, 0.3f, 0.9f, 0.6f};
        const std::vector<float> water = {0.8f, 0.4f, 0.1f, 0.2f};
        const DisplayTerrainPreviewRequest request{std::cref(terrain), std::cref(water), DisplayType::MoistureMap, "terrain_preview"};
        std::string message;
        assert(request.validate(message));

        DisplayManagerParams params;
        const auto buffer = buildDisplayBufferFromTerrain(request, params);
        assert(buffer.label == "Moisture Map");
        assert(buffer.values.size() == terrain.size());
    }

    {
        const std::vector<float> primary = {0.0f, 1.0f, 0.5f, 0.2f};
        const std::vector<float> terrain = {0.1f, 0.2f, 0.8f, 0.7f};
        const std::vector<float> water = {0.7f, 0.5f, 0.1f, 0.0f};
        const std::vector<float> humidity = {0.6f, 0.4f, 0.3f, 0.2f};
        const std::vector<float> windU = {0.0f, 1.0f, -1.0f, 0.5f};
        const std::vector<float> windV = {1.0f, 0.0f, 0.5f, -0.5f};

        const DisplayPreviewComponentsRequest request{
            std::cref(primary),
            std::cref(terrain),
            std::cref(water),
            std::cref(humidity),
            std::cref(windU),
            std::cref(windV),
            DisplayType::WindField,
            "components_preview"};

        std::string message;
        assert(request.validate(message));

        DisplayManagerParams params;
        const auto buffer = buildDisplayBufferFromPreviewComponents(request, params);
        assert(buffer.label == "Wind Field");
        assert(buffer.values.size() == primary.size());
    }

    return 0;
}
