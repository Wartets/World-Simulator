#pragma once

#include "ws/core/state_store.hpp"

#include <string>

namespace ws {

// =============================================================================
// Field Resolver
// =============================================================================

// Resolves semantic field keys to actual field identifiers in the state store.
// Used for mapping abstract field references (e.g., "terrain", "water") to
// concrete variable handles.
class FieldResolver {
public:
    // Resolves a required field by semantic key and returns the field name.
    [[nodiscard]] static std::string resolveRequiredField(
        const StateStore& stateStore,
        const std::string& semanticKey,
        const std::string& consumerName);

    // Resolves a required field by semantic key and returns the field handle.
    [[nodiscard]] static StateStore::FieldHandle resolveRequiredFieldHandle(
        const StateStore& stateStore,
        const std::string& semanticKey,
        const std::string& consumerName);
};

} // namespace ws
