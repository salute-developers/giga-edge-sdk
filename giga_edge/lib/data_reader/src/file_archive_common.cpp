#include "file_archive_common.h"

#include "giga_edge/lib/data_reader/file_path.h"

#include <optional>

namespace giga_edge {
namespace data_reader {

namespace {
inline constexpr int kBlockSize = 10240;
}

bool FileArchiveHelper::Open() {
#if defined(_WIN32)
  return archive_read_open_filename_w(arc_, FilePath(archive_path_).c_str(), kBlockSize) == ARCHIVE_OK;
#else
  return archive_read_open_filename(arc_, archive_path_.c_str(), kBlockSize) == ARCHIVE_OK;
#endif
}

FileArchiveHelper::FileArchiveHelper(const std::string& archive_path, bool add_gzip_filter, bool defer_open)
  : archive_path_(archive_path) {
  if (add_gzip_filter) {
    if (archive_read_support_filter_gzip(arc_) == ARCHIVE_FATAL) {
      ThrowArchiveError("Failed to add support for gzip");
    }
  }

  if (!defer_open && !Open()) {
    ThrowArchiveError("Cannot open archive " + archive_path);
  }
}

std::optional<std::vector<char>> FileArchiveHelper::ReadData(const std::string& file_path) {
  auto [entry_size, entry_position] = SeekEntry(file_path, /*reject_sparse=*/false);
  if (entry_position == 0) {
    return std::nullopt;
  }

  std::vector<char> entry_data(entry_size);
  size_t bytes_read = archive_read_data(arc_, entry_data.data(), entry_data.size());
  if (bytes_read != entry_data.size()) {
    throw ReaderException("Failed to read whole file " + file_path + " in archive " + archive_path_);
  }

  return entry_data;
}

} // namespace data_reader

bool IsUncompressedTarArchive(const std::string& archive_path) {
  return data_reader::FileArchiveHelper(archive_path, false /* no gzip */, true /* defer open */).Open();
}

bool IsTarArchive(const std::string& archive_path) {
  return data_reader::FileArchiveHelper(archive_path, true /* gzip */, true /* defer open */).Open();
}

} // namespace giga_edge
