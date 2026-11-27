#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {
namespace data_reader {
namespace path_utils {

inline std::string NormalizeExtension(std::optional<std::string> extension) {
  if (!extension || extension->empty()) {
    return {};
  }

  if ((*extension)[0] == '.') {
    return *extension;
  }

  return "." + *extension;
}

inline std::string NormalizeDirectory(const std::filesystem::path& directory_path) {
  auto dir = directory_path.generic_string();
  if (dir == "/" || dir == ".") {
    dir.clear();
  }

  while (!dir.empty() && dir.front() == '/') {
    dir.erase(dir.begin());
  }

  if (!dir.empty() && dir.back() != '/') {
    dir.push_back('/');
  }

  return dir;
}

inline std::string NormalizeArchiveEntryPath(std::string entry_path) {
  while (!entry_path.empty() && entry_path.front() == '/') {
    entry_path.erase(entry_path.begin());
  }

  if ((entry_path.size() >= 2) && entry_path[0] == '.' && entry_path[1] == '/') {
    entry_path.erase(0, 2);
  }

  return entry_path;
}

inline std::string NormalizeComparableArchivePath(std::string entry_path) {
  auto normalized = NormalizeArchiveEntryPath(std::move(entry_path));
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

inline std::string NormalizeReaderPath(const std::filesystem::path& path) {
  auto normalized = NormalizeArchiveEntryPath(path.generic_string());
  if (normalized == "." || normalized == "/") {
    return {};
  }

  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }

  return normalized;
}

inline bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline bool MatchesPathOrDescendant(const std::string& normalized_entry_path, const std::string& normalized_target) {
  return normalized_entry_path == normalized_target || StartsWith(normalized_entry_path, normalized_target + "/");
}

inline std::optional<bool> MatchDirectoryPath(
  const std::string& normalized_entry_path, bool is_directory_entry, const std::string& normalized_target
) {
  if (normalized_entry_path == normalized_target) {
    return is_directory_entry;
  }
  if (StartsWith(normalized_entry_path, normalized_target + "/")) {
    return true;
  }
  return std::nullopt;
}

inline bool IsDirectChildInDirectory(const std::string& normalized_entry_path, const std::string& normalized_dir) {
  std::string relative;
  if (normalized_dir.empty()) {
    relative = normalized_entry_path;
  } else {
    if (!StartsWith(normalized_entry_path, normalized_dir)) {
      return false;
    }
    relative = normalized_entry_path.substr(normalized_dir.size());
  }

  return !relative.empty() && relative.find('/') == std::string::npos;
}

// "a/b/c.bin" under "a/" gives "a/b": archives may only imply directories.
inline std::optional<std::string> ChildDirectoryInDirectory(
  const std::string& normalized_entry_path, bool is_directory_entry, const std::string& normalized_dir
) {
  if (!StartsWith(normalized_entry_path, normalized_dir)) {
    return std::nullopt;
  }
  auto relative = normalized_entry_path.substr(normalized_dir.size());
  while (!relative.empty() && relative.back() == '/') {
    relative.pop_back();
  }
  if (relative.empty()) {
    return std::nullopt;
  }
  const auto slash = relative.find('/');
  if (slash == std::string::npos) {
    return is_directory_entry ? std::optional<std::string>(normalized_dir + relative) : std::nullopt;
  }
  return normalized_dir + relative.substr(0, slash);
}

inline std::vector<std::filesystem::path> UniqueSortedPaths(std::vector<std::string> paths) {
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return {paths.begin(), paths.end()};
}

inline bool MatchesExtension(const std::filesystem::path& path, const std::string& normalized_ext) {
  return normalized_ext.empty() || path.extension().string() == normalized_ext;
}

inline void SortPaths(std::vector<std::filesystem::path>& paths) {
  std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.generic_string() < rhs.generic_string();
  });
}

} // namespace path_utils

// nullopt: path is not a regular file; throws ReaderException when stat fails.
std::optional<uintmax_t> RegularFileSize(const std::string& file_path);

} // namespace data_reader
} // namespace giga_edge
