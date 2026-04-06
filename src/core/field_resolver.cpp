#include "ws/core/field_resolver.hpp"

#include <stdexcept>

namespace ws {

// Resolves a required semantic field to its variable ID.
// Throws runtime_error if the semantic key is missing or resolves to unknown variable.
std::string FieldResolver::resolveRequiredField(
    const StateStore& stateStore,
    const std::string& semanticKey,
    const std::string& consumerName) {
    const auto variableId = stateStore.resolveFieldAlias(semanticKey);
    if (!variableId.has_value()) {
        throw std::runtime_error(
            consumerName + ": missing semantic field alias '" + semanticKey + "' in model metadata");
    }
    if (!stateStore.hasField(*variableId)) {
        throw std::runtime_error(
            consumerName + ": semantic alias '" + semanticKey + "' resolved to unknown variable '" + *variableId + "'");
    }
    return *variableId;
}

// Resolves a required semantic field and returns its field handle.
// Throws runtime_error if resolution fails or handle acquisition fails.
StateStore::FieldHandle FieldResolver::resolveRequiredFieldHandle(
    const StateStore& stateStore,
    const std::string& semanticKey,
    const std::string& consumerName) {
    const auto variableId = resolveRequiredField(stateStore, semanticKey, consumerName);
    const auto handle = stateStore.getFieldHandle(variableId);
    if (handle == StateStore::InvalidHandle) {
        throw std::runtime_error(
            consumerName + ": failed to acquire field handle for variable '" + variableId + "'");
    }
    return handle;
}

} // namespace ws
