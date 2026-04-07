#include "ws/gui/session_manager/session_manager.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ws::gui::session_manager {

// Formats byte count as human-readable string.
// Uses IEC units (B, KB, MB, GB, TB) with 1024 base.
// @param bytes Byte count to format
// @return Formatted string (e.g., "1.5 MB")
std::string formatBytes(const std::uintmax_t bytes) {
    static constexpr std::array<const char*, 5> kUnits = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << kUnits[unit];
    return out.str();
}

// Formats filesystem timestamp as local time string.
// @param timePoint Filesystem file time
// @param available Whether timestamp is available
// @return Formatted datetime string or "n/a"
std::string formatFileTime(const std::filesystem::file_time_type& timePoint, const bool available) {
    if (!available) {
        return "n/a";
    }

    try {
        const auto now = std::chrono::system_clock::now();
        const auto fsNow = std::filesystem::file_time_type::clock::now();
        const auto systemTime = now + std::chrono::duration_cast<std::chrono::system_clock::duration>(timePoint - fsNow);
        const std::time_t tt = std::chrono::system_clock::to_time_t(systemTime);

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif

        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    } catch (...) {
        return "n/a";
    }
}

} // namespace ws::gui::session_manager
