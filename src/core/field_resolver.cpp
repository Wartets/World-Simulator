#include "ws/core/field_resolver.hpp"

#include <stdexcept>

namespace ws {

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
