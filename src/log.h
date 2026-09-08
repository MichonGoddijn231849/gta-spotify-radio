#pragma once

#include <filesystem>
#include <string>

namespace gsr::log {

void initialize(const std::filesystem::path& directory);
void info(const std::string& message);
void error(const std::string& message);

} // namespace gsr::log
