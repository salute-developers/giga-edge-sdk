#pragma once

#include <filesystem>
#include <string>

namespace giga_edge {

std::filesystem::path FilePath(const std::string& utf8);

std::string Utf8Path(const std::filesystem::path& path);

} // namespace giga_edge
