#pragma once

#include <cstdint>
#include <string>

namespace ws::gui::crash {

struct CrashReportInput {
    std::string version;
    std::string subsystem;
    std::string stage;
    std::string reason;
    std::string technicalDetail;
    bool unhandled = false;
};

[[nodiscard]] std::string formatCrashReport(const CrashReportInput& input, std::uint64_t unixSeconds);
void installCrashHandlers(const std::string& version);
void setCrashContext(const std::string& subsystem, const std::string& stage);
void recordHandledFailure(const CrashReportInput& input);

} // namespace ws::gui::crash
