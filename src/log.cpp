#include "log.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {

std::mutex g_mutex;
std::filesystem::path g_path;

void write_line(const char* level, const std::string& message) {
    std::lock_guard lock(g_mutex);
    if (g_path.empty()) return;

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);

    std::ofstream stream(g_path, std::ios::app);
    if (!stream) return;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
           << " [" << level << "] " << message << '\n';
}

} // namespace

namespace gsr::log {

void initialize(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::lock_guard lock(g_mutex);
    g_path = directory / "GtaSpotifyRadio.log";
    std::ofstream(g_path, std::ios::trunc) << "GTA Spotify Radio log\n";
}

void info(const std::string& message) { write_line("INFO", message); }
void error(const std::string& message) { write_line("ERROR", message); }

} // namespace gsr::log
