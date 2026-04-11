#include "ws/gui/crash_report.hpp"
#include "ws/gui/build_info.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testCrashReportFormattingIncludesRequiredFields() {
    const ws::gui::crash::CrashReportInput input{
        ws::gui::kApplicationVersion,
        "gui",
        "startup",
        "std_exception",
        "file_not_found",
        false
    };

    const std::string report = ws::gui::crash::formatCrashReport(input, 1700000000ull);

    expect(report.find("world_simulator_crash_report") != std::string::npos, "missing report header");
    expect(report.find("timestamp_unix=1700000000") != std::string::npos, "missing timestamp");
    expect(report.find(std::string{"version="} + ws::gui::kApplicationVersion) != std::string::npos, "missing version");
    expect(report.find("subsystem=gui") != std::string::npos, "missing subsystem");
    expect(report.find("stage=startup") != std::string::npos, "missing stage");
    expect(report.find("handled=yes") != std::string::npos, "handled status mismatch");
    expect(report.find("reason=std_exception") != std::string::npos, "missing reason");
    expect(report.find("technical_detail=file_not_found") != std::string::npos, "missing technical detail");
}

void testUnhandledFlagFormatting() {
    ws::gui::crash::CrashReportInput input{};
    input.unhandled = true;

    const std::string report = ws::gui::crash::formatCrashReport(input, 42ull);
    expect(report.find("handled=no") != std::string::npos, "unhandled status mismatch");
}

} // namespace

int main() {
    try {
        testCrashReportFormattingIncludesRequiredFields();
        testUnhandledFlagFormatting();
    } catch (const std::exception& exception) {
        std::cerr << "crash_report_test_failed error=" << exception.what() << '\n';
        return 1;
    }

    std::cout << "crash_report_test_passed\n";
    return 0;
}
