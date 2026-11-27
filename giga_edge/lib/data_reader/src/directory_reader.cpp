#include "directory_reader.h"

#include "data_reader_common.h"

#include "giga_edge/lib/data_reader/file_path.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {

DirectoryDataReader::DirectoryDataReader(std::string&& directory)
  : directory_(std::move(directory)) {
  if (!::giga_edge::IsDirectory(directory_)) {
    throw ReaderException("Path " + directory_ + " is not a directory");
  }

  if (directory_.back() != '/') {
    directory_.push_back('/');
  }
}

IMovableDataSpanPtr DirectoryDataReader::ReadTransientData(const std::string& file_path) {
  return ReadFileTo<IMovableDataSpanPtr>(directory_ + file_path);
}

IDataSpanPtr DirectoryDataReader::ReadPersistentData(const std::string& file_path) {
  return ReadFileTo<IDataSpanPtr>(directory_ + file_path);
}

std::optional<std::string> DirectoryDataReader::GetPathPrefix() const {
  return directory_;
}

std::vector<std::filesystem::path> DirectoryDataReader::ListFiles(
  const std::filesystem::path& directory_path, std::optional<std::string> extension
) {
  const auto normalized_dir = data_reader::path_utils::NormalizeDirectory(directory_path);
  const auto normalized_ext = data_reader::path_utils::NormalizeExtension(std::move(extension));

  const auto target_dir = directory_ + normalized_dir;
  if (!::giga_edge::IsDirectory(target_dir)) {
    return {};
  }

  std::vector<std::filesystem::path> result;
  for (const auto& entry : std::filesystem::directory_iterator(FilePath(target_dir))) {
    if (!entry.is_regular_file()) {
      continue;
    }

    if (!data_reader::path_utils::MatchesExtension(entry.path(), normalized_ext)) {
      continue;
    }

    result.emplace_back(normalized_dir + Utf8Path(entry.path().filename()));
  }

  data_reader::path_utils::SortPaths(result);
  return result;
}

std::vector<std::filesystem::path> DirectoryDataReader::ListDirectories(const std::filesystem::path& directory_path) {
  const auto normalized_dir = data_reader::path_utils::NormalizeDirectory(directory_path);
  const auto target_dir = directory_ + normalized_dir;
  if (!::giga_edge::IsDirectory(target_dir)) {
    return {};
  }

  std::vector<std::filesystem::path> result;
  for (const auto& entry : std::filesystem::directory_iterator(FilePath(target_dir))) {
    if (entry.is_directory()) {
      result.emplace_back(normalized_dir + Utf8Path(entry.path().filename()));
    }
  }

  data_reader::path_utils::SortPaths(result);
  return result;
}

bool DirectoryDataReader::Exists(const std::filesystem::path& path) {
  const auto normalized_path = data_reader::path_utils::NormalizeReaderPath(path);
  if (normalized_path.empty()) {
    return true;
  }
  return std::filesystem::exists(FilePath(directory_ + normalized_path));
}

bool DirectoryDataReader::IsDirectory(const std::filesystem::path& path) {
  const auto normalized_path = data_reader::path_utils::NormalizeReaderPath(path);
  if (normalized_path.empty()) {
    return true;
  }
  return std::filesystem::is_directory(FilePath(directory_ + normalized_path));
}

IDataReaderPtr CreateDirectoryDataReader(std::string directory_path, DataReaderOptions options) {
  return std::make_shared<DirectoryDataReader>(std::move(directory_path));
}

} // namespace giga_edge
