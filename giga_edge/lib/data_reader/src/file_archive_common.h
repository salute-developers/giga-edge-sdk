#pragma once

#include "archive_common.h"

#include <optional>

namespace giga_edge::data_reader {

class FileArchiveHelper : public BaseTarArchiveHelper {
public:
  // If defer_open == true does not call Open().
  explicit FileArchiveHelper(const std::string& archive_path, bool add_gzip_filter, bool defer_open = false);
  // One-shot, returns true on success.
  bool Open();

  // One-shot.
  std::optional<std::vector<char>> ReadData(const std::string& file_path);

private:
  const std::string& archive_path_; // note a reference, holder is a temporary object
};

} // namespace giga_edge::data_reader
