#pragma once

#include "archive.h"

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/data_reader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge::data_reader {

// One-shot libarchive helper.
class BaseTarArchiveHelper : NonCopyable {
protected:
  BaseTarArchiveHelper();
  ~BaseTarArchiveHelper();

  void Free();

public:
  // Finds the entry by calling archive_read_next_header in a loop, which moves the read position of arc_.
  // libarchive reads in a single pass, so call it at most once per helper.
  // reject_sparse: sparse entries store fewer bytes than the logical size, so the window
  // [offset, size) crosses into neighboring entries - readers that hand out raw windows must
  // reject such entries; readers going through archive_read_data are indifferent
  std::pair<la_int64_t /*size*/, la_int64_t /*offset from archive start*/> SeekEntry(
    const std::string& path, bool reject_sparse
  );

  // Attention: one-shot like SeekEntry - a single pass over a fresh helper
  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  );
  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path);
  bool Exists(const std::filesystem::path& path);
  bool IsDirectory(const std::filesystem::path& path);

  void ThrowArchiveError(std::string msg);
  std::string GetArchiveErrorString() const;

private:
  void ThrowArchiveErrorImpl(std::string msg, bool should_free);

protected:
  archive* arc_;
};

} // namespace giga_edge::data_reader
