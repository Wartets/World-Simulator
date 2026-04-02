#include "ws/app/shell_support.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void testNormalizeModelKey() {
    assert(ws::app::normalizeModelKey("  models/My Model.simmodel  ") == "My_Model");
    assert(ws::app::normalizeModelKey("alpha.simmodel") == "alpha");
    assert(ws::app::normalizeModelKey("   ").empty());
}

void testListAvailableModels() {
    const auto tempRoot = std::filesystem::temp_directory_path() / "world_simulator_shell_support_test";
    const auto modelsRoot = tempRoot / "models";
    const auto first = modelsRoot / "alpha.simmodel";
    const auto second = modelsRoot / "beta.simmodel";

    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);

    {
        std::ofstream(first / "model.json") << "{\"id\":\"alpha\"}";
    }
    {
        std::ofstream(second / "model.json") << "{\"id\":\"beta\"}";
    }

    const auto entries = ws::app::listAvailableModels(modelsRoot);
    assert(entries.size() == 2u);
    assert(entries[0].key == "alpha");
    assert(entries[0].isDirectory);
    assert(entries[1].key == "beta");
    assert(entries[1].isDirectory);

    std::filesystem::remove_all(tempRoot);
}

} // namespace

int main() {
    testNormalizeModelKey();
    testListAvailableModels();
    std::cout << "shell_support tests passed.\n";
    return 0;
}
