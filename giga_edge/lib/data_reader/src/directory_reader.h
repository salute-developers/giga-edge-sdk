#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

class DirectoryDataReader : public IDataReader {
public:
  explicit DirectoryDataReader(std::string&& directory);

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override;
  IDataSpanPtr ReadPersistentData(const std::string& file_path) override;
  std::optional<std::string> GetPathPrefix() const override;
  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  ) override;
  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path) override;
  bool Exists(const std::filesystem::path& path) override;
  bool IsDirectory(const std::filesystem::path& path) override;

protected:
  std::string directory_;
};

} // namespace giga_edge
