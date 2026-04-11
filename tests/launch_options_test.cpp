#include "ws/gui/launch_options.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testModelSelectionFlag() {
    const auto parsed = ws::gui::parseLaunchOptions({"--model", "models/game_of_life_model.simmodel"});
    expect(parsed.ok, "--model should parse successfully");
    expect(!parsed.showHelp, "--model should not trigger help");
    expect(parsed.request.action == ws::gui::LaunchAction::SelectModelForSession, "--model should select model for session");
    expect(parsed.request.targetPath.string() == "models/game_of_life_model.simmodel", "model target path mismatch");
    expect(parsed.request.modelPath.string() == "models/game_of_life_model.simmodel", "model scope path mismatch");
}

void testPositionalSimmodelRoutesToEditor() {
    const auto parsed = ws::gui::parseLaunchOptions({"models/game_of_life_model.simmodel"});
    expect(parsed.ok, "positional .simmodel should parse successfully");
    expect(parsed.request.action == ws::gui::LaunchAction::OpenModelEditor, "positional .simmodel should route to model editor");
}

void testPositionalCheckpointRoutesToCheckpointLoader() {
    const auto parsed = ws::gui::parseLaunchOptions({"checkpoints/worlds/world_0001.wscp"});
    expect(parsed.ok, "positional .wscp should parse successfully");
    expect(parsed.request.action == ws::gui::LaunchAction::OpenCheckpointFile, "positional .wscp should route to checkpoint loader");
}

void testWorldOpenWithModelScope() {
    const auto parsed = ws::gui::parseLaunchOptions({"--model", "models/environmental_model_2d.simmodel", "--world", "world_0001"});
    expect(parsed.ok, "--model + --world should parse successfully");
    expect(parsed.request.action == ws::gui::LaunchAction::OpenWorldByName, "--world should route to world open action");
    expect(parsed.request.worldName == "world_0001", "world name mismatch");
    expect(parsed.request.modelPath.string() == "models/environmental_model_2d.simmodel", "model scope path mismatch");
}

void testConflictingPrimaryTargetsFail() {
    const auto parsed = ws::gui::parseLaunchOptions({"--world", "world_0001", "--checkpoint", "checkpoints/worlds/world_0001.wscp"});
    expect(!parsed.ok, "conflicting primary targets should fail");
}

void testUnsupportedOpenExtensionFails() {
    const auto parsed = ws::gui::parseLaunchOptions({"--open", "notes.txt"});
    expect(!parsed.ok, "unsupported --open extension should fail");
}

} // namespace

int main() {
    try {
        testModelSelectionFlag();
        testPositionalSimmodelRoutesToEditor();
        testPositionalCheckpointRoutesToCheckpointLoader();
        testWorldOpenWithModelScope();
        testConflictingPrimaryTargetsFail();
        testUnsupportedOpenExtensionFails();
    } catch (const std::exception& exception) {
        std::cerr << "launch_options_test_failed error=" << exception.what() << '\n';
        return 1;
    }

    std::cout << "launch_options_test_passed\n";
    return 0;
}
