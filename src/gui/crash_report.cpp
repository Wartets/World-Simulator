#include "ws/gui/crash_report.hpp"

#include "ws/gui/storage_paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ws::gui::crash {
namespace {

std::mutex gCrashMutex;
std::string gCrashVersion = "unknown";
std::string gSubsystem = "gui";
std::string gStage = "runtime";
std::atomic<std::uint64_t> gCrashSequence{0};

[[nodiscard]] std::uint64_t currentUnixSeconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

[[nodiscard]] std::filesystem::path resolveCrashDirectory() {
    const std::filesystem::path root = storage::resolveUserSettingsRoot("WorldSimulator");
    const std::filesystem::path dir = root / "crash_reports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

[[nodiscard]] std::string sanitizeToken(std::string token) {
    if (token.empty()) {
        return "unknown";
    }
    for (char& c : token) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        if (!ok) {
            c = '_';
        }
    }
    return token;
}

[[nodiscard]] std::filesystem::path nextCrashPath(const std::string& subsystem, const std::string& stage) {
    const std::uint64_t ts = currentUnixSeconds();
    const std::uint64_t seq = gCrashSequence.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream filename;
    filename << "crash_" << ts << "_" << sanitizeToken(subsystem) << "_" << sanitizeToken(stage) << "_" << seq << ".log";
    return resolveCrashDirectory() / filename.str();
}

void writeCrashReportToDisk(const CrashReportInput& input) {
    const std::uint64_t unixSeconds = currentUnixSeconds();
    const std::filesystem::path outputPath = nextCrashPath(input.subsystem, input.stage);

    std::ofstream out(outputPath, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << formatCrashReport(input, unixSeconds);
}

[[noreturn]] void terminateHandler() {
    CrashReportInput input;
    {
        const std::lock_guard<std::mutex> lock(gCrashMutex);
        input.version = gCrashVersion;
        input.subsystem = gSubsystem;
        input.stage = gStage;
    }
    input.reason = "unhandled_terminate";
    input.unhandled = true;

    try {
        const std::exception_ptr exception = std::current_exception();
        if (exception) {
            std::rethrow_exception(exception);
        }
        input.technicalDetail = "terminate called without active exception";
    } catch (const std::exception& exception) {
        input.technicalDetail = exception.what();
    } catch (...) {
        input.technicalDetail = "unknown non-std exception";
    }

    writeCrashReportToDisk(input);
    std::abort();
}

#ifdef _WIN32
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    CrashReportInput input;
    {
        const std::lock_guard<std::mutex> lock(gCrashMutex);
        input.version = gCrashVersion;
        input.subsystem = gSubsystem;
        input.stage = gStage;
    }
    input.reason = "structured_exception";
    input.unhandled = true;

    std::ostringstream detail;
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        detail << "seh_code=0x" << std::hex << info->ExceptionRecord->ExceptionCode;
    } else {
        detail << "seh_code=unknown";
    }
    input.technicalDetail = detail.str();
    writeCrashReportToDisk(input);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

std::string formatCrashReport(const CrashReportInput& input, const std::uint64_t unixSeconds) {
    std::ostringstream out;
    out << "world_simulator_crash_report\n";
    out << "timestamp_unix=" << unixSeconds << '\n';
    out << "version=" << (input.version.empty() ? "unknown" : input.version) << '\n';
    out << "subsystem=" << (input.subsystem.empty() ? "unknown" : input.subsystem) << '\n';
    out << "stage=" << (input.stage.empty() ? "unknown" : input.stage) << '\n';
    out << "handled=" << (input.unhandled ? "no" : "yes") << '\n';
    out << "reason=" << (input.reason.empty() ? "unknown" : input.reason) << '\n';
    out << "technical_detail=" << (input.technicalDetail.empty() ? "n/a" : input.technicalDetail) << '\n';
    return out.str();
}

void installCrashHandlers(const std::string& version) {
    {
        const std::lock_guard<std::mutex> lock(gCrashMutex);
        gCrashVersion = version.empty() ? "unknown" : version;
    }

    std::set_terminate(terminateHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif
}

void setCrashContext(const std::string& subsystem, const std::string& stage) {
    const std::lock_guard<std::mutex> lock(gCrashMutex);
    gSubsystem = subsystem.empty() ? "gui" : subsystem;
    gStage = stage.empty() ? "runtime" : stage;
}

void recordHandledFailure(const CrashReportInput& input) {
    CrashReportInput report = input;
    report.unhandled = false;

    if (report.version.empty() || report.subsystem.empty() || report.stage.empty()) {
        const std::lock_guard<std::mutex> lock(gCrashMutex);
        if (report.version.empty()) {
            report.version = gCrashVersion;
        }
        if (report.subsystem.empty()) {
            report.subsystem = gSubsystem;
        }
        if (report.stage.empty()) {
            report.stage = gStage;
        }
    }

    writeCrashReportToDisk(report);
}

} // namespace ws::gui::crash
