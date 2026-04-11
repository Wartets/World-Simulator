// =============================================================================
// Display Manager Public API
// =============================================================================
/// This module provides display buffer construction from simulation state.
///
/// ## Public API Contract
/// All public functions, enums, and request/parameter structures are stable
/// for external use. Client code may depend on these interfaces without
/// breaking on patch releases. Internal display computation algorithms may
/// be refactored without changing public contracts.
///
/// ## Display Types and Semantics
/// DisplayType enumerates visualization modes:
/// - **ScalarField**: Raw values from the selected field, mapped to [0, 1].
/// - **SurfaceCategory**: Categorized surface type (water, land, highland, etc.).
/// - **RelativeElevation**: Elevation relative to water level.
/// - **WaterDepth**: Water depth (where applicable).
/// - **MoistureMap**: Soil or atmosphere moisture visualization.
/// - **WindField**: Wind vector magnitude/direction visualization.
///
/// ## Request Pattern
/// All display building is done through typed request objects:
/// - **DisplaySnapshotRequest**: Generate from runtime state snapshot.
/// - **DisplayTerrainPreviewRequest**: Preview from terrain/water preview arrays.
/// - **DisplayPreviewComponentsRequest**: Preview from full component set.
///
/// Each request object provides:
/// - `validate()`: Check preconditions (non-null fields, valid indices, finite values).
/// - `sanitized()`: Return a safe parameter set with invalid values corrected.
///
/// ## Parameter Semantics
/// DisplayManagerParams controls the interpretation of field values:
/// - **autoWaterLevel**: If true, compute water level from field quantile.
/// - **waterLevel**: Explicit water level threshold (used if !autoWaterLevel).
/// - **Thresholds**: Used to classify elevation ranges (lowland, highland, etc.).
///
/// All thresholds should be in [0, 1] range normalized to the field domain.
/// Out-of-range values are automatically clamped by sanitized().
///
/// ## Error Semantics
/// - Request validation (validate()) returns false if preconditions fail.
/// - Display building never throws; instead returns a best-effort buffer.
/// - Backward-compatible overloads maintain legacy API while new request types
///   provide better type safety and constraint documentation.
///
/// ## Thread Safety
/// All functions are stateless (except for field/water array references passed
/// by const-reference in request objects). Display building is safe to call
/// concurrently with the same request/params, though distinct requests should
/// have distinct threads to avoid contention on the returned DisplayBuffer.
///
// Public contract: DisplayType, DisplayManagerParams, display buffer functions,
// and request structures are stable for external use.
//
// Implementation detail: Internal display computation logic may be refactored
// without changing the public interface contracts.
// =============================================================================

#pragma once

#include "ws/core/state_store.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::gui {

// =============================================================================
// Display Type
// =============================================================================

/// Types of visualization displays available.
/// Each type specifies how field values are transformed for rendering.
enum class DisplayType {
    ScalarField = 0,       ///< Raw scalar field visualization; values mapped to [0, 1].
    SurfaceCategory = 1,   ///< Categorized surface type visualization (water, land, etc.).
    RelativeElevation = 2, ///< Relative elevation from water level.
    WaterDepth = 3,        ///< Water depth visualization.
    MoistureMap = 4,       ///< Soil moisture map visualization.
    WindField = 5          ///< Wind vector field magnitude visualization.
};

// =============================================================================
// Display Manager Params
// =============================================================================

/// Parameters for display buffer construction.
/// Controls water level detection, categorization thresholds, and rendering hints.
struct DisplayManagerParams {
    bool autoWaterLevel = true;            ///< Whether to auto-calculate water level from field quantile.
    float waterLevel = 0.48f;              ///< Manual water level threshold (used if !autoWaterLevel).
    float autoWaterQuantile = 0.58f;       ///< Quantile (0-1) to use for auto water level calculation.
    float lowlandThreshold = 0.58f;        ///< Threshold for lowland vs. medium elevation classification.
    float highlandThreshold = 0.75f;       ///< Threshold for highland elevation classification.
    float waterPresenceThreshold = 0.12f;  ///< Minimum value for water presence detection.
    float shallowWaterDepth = 0.05f;       ///< Depth threshold for shallow water categorization.
    float highMoistureThreshold = 0.65f;   ///< Threshold for high moisture classification.

    /// Returns a clamped/safe parameter set suitable for rendering.
    /// All thresholds are clamped to [0, 1]; quantiles to [0, 1].
    /// Non-finite values are reset to defaults.
    [[nodiscard]] DisplayManagerParams sanitized() const;

    /// Validates the parameter set.
    /// Returns false and sets errorMessage if any threshold value is non-finite.
    /// Use this before passing to display building functions for strict checking.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

/// Returns an empty, reusable display-tag map (immutable singleton).
/// Safe to call concurrently.
[[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>>& emptyDisplayFieldTags();

// =============================================================================
// Display Snapshot Request
// =============================================================================

/// Typed request for snapshot-driven display generation.
/// Specifies which field to visualize and how to transform it.
struct DisplaySnapshotRequest {
    const StateStoreSnapshot& snapshot;                                         ///< Source state snapshot (must remain valid during building).
    int primaryFieldIndex = 0;                                                  ///< Index of field to visualize (must be < snapshot.cellScalarFields.size()).
    DisplayType displayType = DisplayType::ScalarField;                         ///< Type of display to build.
    bool includeSparseOverlay = false;                                          ///< Whether to overlay sparse variable visualization.
    std::reference_wrapper<const std::unordered_map<std::string, std::vector<std::string>>> fieldDisplayTags =
        std::cref(emptyDisplayFieldTags());                                     ///< Display tags for field metadata (UI hints).

    /// Returns a sanitized copy with valid indices and finite parameters.
    [[nodiscard]] DisplaySnapshotRequest sanitized() const;

    /// Validates the request.
    /// Returns false and sets errorMessage if primaryFieldIndex is out of range
    /// or snapshot is invalid. Call before building for strict checking.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// =============================================================================
// Display Terrain Preview Request
// =============================================================================

/// Typed request for terrain/water preview display generation.
/// Used during world creation preview before simulation starts.
struct DisplayTerrainPreviewRequest {
    std::reference_wrapper<const std::vector<float>> terrain;                   ///< Terrain elevation array (must match snapshot grid size).
    std::reference_wrapper<const std::vector<float>> water;                     ///< Water depth array (must match snapshot grid size).
    DisplayType displayType = DisplayType::ScalarField;                         ///< Type of display to build.
    std::string label = "preview";                                              ///< Human-readable label for the buffer (e.g., for UI).

    /// Returns a sanitized copy with non-null arrays and finite values.
    [[nodiscard]] DisplayTerrainPreviewRequest sanitized() const;

    /// Validates the request.
    /// Returns false and sets errorMessage if arrays are invalid or mismatched.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// =============================================================================
// Display Preview Components Request
// =============================================================================

/// Typed request for full-component preview display generation.
/// Includes primary field plus terrain, water, humidity, wind components.
struct DisplayPreviewComponentsRequest {
    std::reference_wrapper<const std::vector<float>> primary;                   ///< Primary field array (visualization target).
    std::reference_wrapper<const std::vector<float>> terrain;                   ///< Terrain elevation array.
    std::reference_wrapper<const std::vector<float>> water;                     ///< Water depth array.
    std::reference_wrapper<const std::vector<float>> humidity;                  ///< Humidity/moisture array.
    std::reference_wrapper<const std::vector<float>> windU;                     ///< U-component of wind vector.
    std::reference_wrapper<const std::vector<float>> windV;                     ///< V-component of wind vector.
    DisplayType displayType = DisplayType::ScalarField;                         ///< Type of display to build.
    std::string label = "preview";                                              ///< Human-readable label for the buffer.

    /// Returns a sanitized copy with non-null arrays and finite values.
    [[nodiscard]] DisplayPreviewComponentsRequest sanitized() const;

    /// Validates the request.
    /// Returns false and sets errorMessage if any array is invalid or mismatched.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// =============================================================================
// Display Buffer
// =============================================================================

/// Pre-computed buffer for rendering a display type.
/// Contains transformed values ready for visualization.
struct DisplayBuffer {
    std::vector<float> values;          ///< Pre-computed display values, one per grid cell; finite values in [0, 1] typical range.
    float minValue = 0.0f;              ///< Minimum value in the buffer (for range statistics).
    float maxValue = 1.0f;              ///< Maximum value in the buffer (for range statistics).
    float effectiveWaterLevel = 0.48f;  ///< Effective water level used during building (for UI feedback).
    std::string label;                  ///< Label for the buffer (e.g., "ScalarField", "preview").
};

/// Returns a human-readable label for a display type.
/// Example: displayTypeLabel(DisplayType::ScalarField) -> "Scalar Field".
[[nodiscard]] const char* displayTypeLabel(DisplayType type);

// =============================================================================
// Display Building Functions (Request-based, type-safe)
// =============================================================================

/// Builds a display buffer from a state store snapshot (type-safe variant).
/// - request: specifies field index, display type, and overlay options.
/// - params: controls water level, thresholds, and rendering hints.
/// - Returns: a DisplayBuffer with computed values ready for visualization.
/// Precondition: request.snapshot must be valid and request.primaryFieldIndex < snapshot fields.
/// This function never throws; it returns a best-effort buffer even if inputs are invalid.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromSnapshot(
    const DisplaySnapshotRequest& request,
    const DisplayManagerParams& params);

/// Builds a display buffer from terrain and water arrays (type-safe variant).
/// - request: specifies display type and label.
/// - params: controls water level and thresholds.
/// - Returns: a DisplayBuffer with computed values ready for visualization.
/// Precondition: request.terrain and request.water must be valid.
/// This function never throws; it returns a best-effort buffer even if inputs are invalid.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromTerrain(
    const DisplayTerrainPreviewRequest& request,
    const DisplayManagerParams& params);

/// Builds a display buffer from preview component arrays (type-safe variant).
/// - request: specifies display type, label, and all component arrays.
/// - params: controls water level and thresholds.
/// - Returns: a DisplayBuffer with computed values ready for visualization.
/// Precondition: all arrays in request must be valid and same size.
/// This function never throws; it returns a best-effort buffer even if inputs are invalid.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromPreviewComponents(
    const DisplayPreviewComponentsRequest& request,
    const DisplayManagerParams& params);

// =============================================================================
// Backward-Compatible Overloads (Legacy API)
// =============================================================================

/// Backward-compatible overload for snapshot-based display building.
/// Functionally equivalent to the type-safe variant but uses loose parameters.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromSnapshot(
    const StateStoreSnapshot& snapshot,
    int primaryFieldIndex,
    DisplayType displayType,
    bool includeSparseOverlay,
    const DisplayManagerParams& params,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags = {});

/// Backward-compatible overload for terrain preview building.
/// Functionally equivalent to the type-safe variant but uses loose parameters.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromTerrain(
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label = "preview");

/// Backward-compatible overload for preview components building.
/// Functionally equivalent to the type-safe variant but uses loose parameters.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromPreviewComponents(
    const std::vector<float>& primary,
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    const std::vector<float>& humidity,
    const std::vector<float>& windU,
    const std::vector<float>& windV,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label = "preview");

} // namespace ws::gui
