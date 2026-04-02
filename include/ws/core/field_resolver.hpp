#pragma once

#include "ws/core/state_store.hpp"

#include <string>

namespace ws {

class FieldResolver {
public:
    [[nodiscard]] static std::string resolveRequiredField(
        const StateStore& stateStore,
        const std::string& semanticKey,
        const std::string& consumerName);

    [[nodiscard]] static StateStore::FieldHandle resolveRequiredFieldHandle(
        const StateStore& stateStore,
        const std::string& semanticKey,
        const std::string& consumerName);
};

} // namespace ws
