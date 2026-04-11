#include "ws/gui/launch_options.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace ws::gui {
namespace {

[[nodiscard]] std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] bool hasValueFlag(const std::string& flag) {
    return flag == "--model" ||
           flag == "--edit-model" ||
           flag == "--world" ||
           flag == "--import-world" ||
           flag == "--checkpoint" ||
           flag == "--open";
}

[[nodiscard]] LaunchAction actionFromFilePath(const std::filesystem::path& path) {
    const std::string extension = toLower(path.extension().string());
    if (extension == ".simmodel") {
        return LaunchAction::OpenModelEditor;
    }
    if (extension == ".wscp") {
        return LaunchAction::OpenCheckpointFile;
    }
    if (extension == ".wsexp" || extension == ".wsworld") {
        return LaunchAction::ImportWorldFile;
    }
    return LaunchAction::None;
}

[[nodiscard]] LaunchParseResult invalidResult(const std::string& error) {
    LaunchParseResult result;
    result.ok = false;
    result.error = error;
    return result;
}

} // namespace

const char* launchActionLabel(const LaunchAction action) {
    switch (action) {
        case LaunchAction::None: return "none";
        case LaunchAction::OpenModelEditor: return "open_model_editor";
        case LaunchAction::SelectModelForSession: return "select_model_for_session";
        case LaunchAction::OpenWorldByName: return "open_world_by_name";
        case LaunchAction::ImportWorldFile: return "import_world_file";
        case LaunchAction::OpenCheckpointFile: return "open_checkpoint_file";
        default: return "unknown";
    }
}

LaunchParseResult parseLaunchOptions(const std::vector<std::string>& args) {
    LaunchParseResult result;

    std::filesystem::path explicitModelPath;
    std::filesystem::path explicitEditModelPath;
    std::string explicitWorldName;
    std::filesystem::path explicitImportWorldPath;
    std::filesystem::path explicitCheckpointPath;
    std::filesystem::path explicitOpenPath;
    std::vector<std::filesystem::path> positionalPaths;

    auto requireNextValue = [&](const std::size_t index, const std::string& flag) -> std::optional<std::string> {
        if ((index + 1u) >= args.size()) {
            result = invalidResult("missing value for " + flag);
            return std::nullopt;
        }
        return args[index + 1u];
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string arg = args[i];
        if (arg == "--help" || arg == "-h") {
            result.showHelp = true;
            return result;
        }

        if (arg == "--model") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitModelPath = *value;
            ++i;
            continue;
        }

        if (arg == "--edit-model") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitEditModelPath = *value;
            ++i;
            continue;
        }

        if (arg == "--world") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitWorldName = *value;
            ++i;
            continue;
        }

        if (arg == "--import-world") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitImportWorldPath = *value;
            ++i;
            continue;
        }

        if (arg == "--checkpoint") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitCheckpointPath = *value;
            ++i;
            continue;
        }

        if (arg == "--open") {
            const auto value = requireNextValue(i, arg);
            if (!value.has_value()) {
                return result;
            }
            explicitOpenPath = *value;
            ++i;
            continue;
        }

        if (arg.rfind("--", 0) == 0) {
            result = invalidResult("unknown option: " + arg);
            return result;
        }

        positionalPaths.emplace_back(arg);
    }

    int primarySpecifierCount = 0;
    primarySpecifierCount += explicitEditModelPath.empty() ? 0 : 1;
    primarySpecifierCount += explicitWorldName.empty() ? 0 : 1;
    primarySpecifierCount += explicitImportWorldPath.empty() ? 0 : 1;
    primarySpecifierCount += explicitCheckpointPath.empty() ? 0 : 1;
    primarySpecifierCount += explicitOpenPath.empty() ? 0 : 1;

    if (primarySpecifierCount > 1) {
        return invalidResult("conflicting launch targets: choose only one of --edit-model, --world, --import-world, --checkpoint, or --open");
    }

    if (!positionalPaths.empty() && (primarySpecifierCount > 0 || !explicitModelPath.empty())) {
        return invalidResult("positional file arguments cannot be combined with explicit launch options");
    }

    if (positionalPaths.size() > 1u) {
        return invalidResult("only one positional file path is supported");
    }

    if (!explicitEditModelPath.empty()) {
        result.request.action = LaunchAction::OpenModelEditor;
        result.request.targetPath = explicitEditModelPath;
    } else if (!explicitWorldName.empty()) {
        result.request.action = LaunchAction::OpenWorldByName;
        result.request.worldName = explicitWorldName;
    } else if (!explicitImportWorldPath.empty()) {
        result.request.action = LaunchAction::ImportWorldFile;
        result.request.targetPath = explicitImportWorldPath;
    } else if (!explicitCheckpointPath.empty()) {
        result.request.action = LaunchAction::OpenCheckpointFile;
        result.request.targetPath = explicitCheckpointPath;
    } else if (!explicitOpenPath.empty()) {
        const LaunchAction inferred = actionFromFilePath(explicitOpenPath);
        if (inferred == LaunchAction::None) {
            return invalidResult("unsupported file extension for --open; expected .simmodel, .wscp, .wsexp, or .wsworld");
        }
        result.request.action = inferred;
        result.request.targetPath = explicitOpenPath;
    } else if (!positionalPaths.empty()) {
        const LaunchAction inferred = actionFromFilePath(positionalPaths.front());
        if (inferred == LaunchAction::None) {
            return invalidResult("unsupported positional file extension; expected .simmodel, .wscp, .wsexp, or .wsworld");
        }
        result.request.action = inferred;
        result.request.targetPath = positionalPaths.front();
    } else if (!explicitModelPath.empty()) {
        result.request.action = LaunchAction::SelectModelForSession;
        result.request.targetPath = explicitModelPath;
    }

    if (!explicitModelPath.empty()) {
        result.request.modelPath = explicitModelPath;
    }

    return result;
}

} // namespace ws::gui
