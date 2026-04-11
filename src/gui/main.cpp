#include "ws/gui/main_window.hpp"
#include "ws/gui/exception_message.hpp"
#include "ws/gui/build_info.hpp"
#include "ws/gui/launch_options.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// GUI application entry point.
// Creates and runs the main window with exception handling.
int GuiMain(const std::vector<std::string>& args) {
    const ws::gui::LaunchParseResult launch = ws::gui::parseLaunchOptions(args);
    if (launch.showHelp) {
        std::cout
            << "World Simulator GUI launch options\n"
            << "  --model <path>         Select model scope and open Session Manager\n"
            << "  --edit-model <path>    Open model directly in Model Editor\n"
            << "  --world <name>         Open stored world by name\n"
            << "  --import-world <path>  Import world package file and open it\n"
            << "  --checkpoint <path>    Start runtime and load checkpoint file\n"
            << "  --open <path>          Open by file extension (.simmodel, .wscp, .wsexp, .wsworld)\n"
            << "  <path>                 Positional file-open path with extension routing\n";
        return 0;
    }

    if (!launch.ok) {
        std::cerr << "invalid_launch_arguments=" << launch.error << std::endl;
        return 2;
    }

    try {
        ws::gui::MainWindow window(launch.request);
        return window.run();
    } catch (const std::exception& exception) {
        const auto translated = ws::gui::translateExceptionMessage(
            exception,
            "GUI startup failed",
            "Check the configuration, model files, and available system resources, then retry.");
        std::cerr << translated.userMessage << '\n'
                  << translated.technicalDetail << " | version=" << ws::gui::kApplicationVersion << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "What happened: GUI startup failed | Why: an unknown exception was raised | Next: Check the configuration, model files, and available system resources, then retry.\n"
                  << "GUI startup failed | unknown exception | version=" << ws::gui::kApplicationVersion << std::endl;
        return 1;
    }
}

// Standard console entry point.
int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return GuiMain(args);
}

#ifdef _WIN32
namespace {

std::string narrowUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required, nullptr, nullptr);
    output.resize(static_cast<std::size_t>(required - 1));
    return output;
}

std::vector<std::string> collectWinMainArgs() {
    std::vector<std::string> args;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }

    if (argc <= 1) {
        LocalFree(argv);
        return args;
    }

    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
        args.push_back(narrowUtf8(argv[i] == nullptr ? L"" : argv[i]));
    }

    LocalFree(argv);
    return args;
}

} // namespace

// Windows GUI entry point (WinMain).
// Allows application to be launched without console window.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return GuiMain(collectWinMainArgs());
}
#endif
