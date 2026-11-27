#include "archive_common.h"

#include "archive.h"
#include "archive_entry.h"
#include "data_reader_common.h"

#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge::data_reader {

namespace {

// On its first call archive_read_new() fills a shared libarchive table without synchronization, and archives of
// model parts are opened from submodels that load in parallel: the first call is made exactly once.
archive* NewArchiveRead() {
  static std::once_flag vtable_ready;
  std::call_once(vtable_ready, [] {
    archive_read_free(archive_read_new());
  });
  return archive_read_new();
}

// archive_read_next_header under a shared mutex: while reading a tar header libarchive changes a shared static
// counter (inode numbers of entries), and archives of model parts are read from submodels that load in parallel.
int ReadNextHeader(archive* arc, archive_entry** entry) {
  static std::mutex mutex;
  const std::lock_guard lock(mutex);
  return archive_read_next_header(arc, entry);
}

} // namespace

BaseTarArchiveHelper::BaseTarArchiveHelper()
  : arc_(NewArchiveRead()) {
  if (archive_read_support_format_tar(arc_) != ARCHIVE_OK) {
    ThrowArchiveErrorImpl("Failed to add support for tar", true);
  }
}

BaseTarArchiveHelper::~BaseTarArchiveHelper() {
  Free();
}

void BaseTarArchiveHelper::Free() {
  if (archive_read_free(arc_) != ARCHIVE_OK) {
    std::cerr << "ERROR: Failed to free archive: " << GetArchiveErrorString() << '\n';
  }
}

std::string BaseTarArchiveHelper::GetArchiveErrorString() const {
  auto error_string = archive_error_string(arc_);
  return error_string == nullptr ? "No error string" : error_string;
}

void BaseTarArchiveHelper::ThrowArchiveErrorImpl(std::string msg, bool should_free) {
  (msg += ": ") += GetArchiveErrorString();

  if (should_free) {
    Free();
  }

  throw ReaderException(msg);
}

void BaseTarArchiveHelper::ThrowArchiveError(std::string msg) {
  ThrowArchiveErrorImpl(std::move(msg), false);
}

std::pair<la_int64_t, la_int64_t> BaseTarArchiveHelper::SeekEntry(const std::string& path, bool reject_sparse) {
  archive_entry* entry;
  int status;
  while ((status = ReadNextHeader(arc_, &entry)) != ARCHIVE_EOF) {
    if (status != ARCHIVE_OK) {
      ThrowArchiveError("Failed to read a tar entry header");
    }

    if (
      archive_entry_filetype(entry) == AE_IFREG && archive_entry_hardlink(entry) == nullptr
      && path == archive_entry_pathname(entry)
    ) {
      if (reject_sparse && archive_entry_sparse_count(entry) > 0) {
        throw ReaderException("Sparse tar entries are not supported, repack the archive as dense: " + path);
      }
      return {archive_entry_size(entry), archive_filter_bytes(arc_, 0)};
    }
  }

  return {0, 0};
}

std::vector<std::filesystem::path> BaseTarArchiveHelper::ListFiles(
  const std::filesystem::path& directory_path, std::optional<std::string> extension
) {
  const auto normalized_dir = path_utils::NormalizeDirectory(directory_path);
  const auto normalized_ext = path_utils::NormalizeExtension(std::move(extension));

  std::vector<std::filesystem::path> result;
  archive_entry* entry = nullptr;
  int status = ARCHIVE_OK;
  while ((status = ReadNextHeader(arc_, &entry)) != ARCHIVE_EOF) {
    if (status != ARCHIVE_OK) {
      ThrowArchiveError("Failed to read archive entry header while listing files");
    }

    if (archive_entry_filetype(entry) != AE_IFREG) {
      continue;
    }

    auto path = path_utils::NormalizeArchiveEntryPath(archive_entry_pathname(entry));
    if (path.empty()) {
      continue;
    }

    if (!path_utils::IsDirectChildInDirectory(path, normalized_dir)) {
      continue;
    }

    const auto entry_path = std::filesystem::path(path);
    if (!path_utils::MatchesExtension(entry_path, normalized_ext)) {
      continue;
    }

    result.push_back(entry_path);
  }

  path_utils::SortPaths(result);
  return result;
}

std::vector<std::filesystem::path> BaseTarArchiveHelper::ListDirectories(const std::filesystem::path& directory_path) {
  const auto normalized_dir = path_utils::NormalizeDirectory(directory_path);
  std::vector<std::string> result;
  archive_entry* entry = nullptr;
  int status = ARCHIVE_OK;
  while ((status = ReadNextHeader(arc_, &entry)) != ARCHIVE_EOF) {
    if (status != ARCHIVE_OK) {
      ThrowArchiveError("Failed to read archive entry header while listing directories");
    }

    const auto path = path_utils::NormalizeArchiveEntryPath(archive_entry_pathname(entry));
    if (
      auto child = path_utils::ChildDirectoryInDirectory(
        path, archive_entry_filetype(entry) == AE_IFDIR, normalized_dir
      )
    ) {
      result.push_back(std::move(*child));
    }
  }

  return path_utils::UniqueSortedPaths(std::move(result));
}

bool BaseTarArchiveHelper::Exists(const std::filesystem::path& path) {
  const auto target = path_utils::NormalizeReaderPath(path);
  if (target.empty()) {
    return true;
  }

  archive_entry* entry = nullptr;
  int status = ARCHIVE_OK;
  while ((status = ReadNextHeader(arc_, &entry)) != ARCHIVE_EOF) {
    if (status != ARCHIVE_OK) {
      ThrowArchiveError("Failed to read archive entry header while checking path existence");
    }

    auto entry_path = path_utils::NormalizeComparableArchivePath(archive_entry_pathname(entry));
    if (path_utils::MatchesPathOrDescendant(entry_path, target)) {
      return true;
    }
  }

  return false;
}

bool BaseTarArchiveHelper::IsDirectory(const std::filesystem::path& path) {
  const auto target = path_utils::NormalizeReaderPath(path);
  if (target.empty()) {
    return true;
  }

  archive_entry* entry = nullptr;
  int status = ARCHIVE_OK;
  while ((status = ReadNextHeader(arc_, &entry)) != ARCHIVE_EOF) {
    if (status != ARCHIVE_OK) {
      ThrowArchiveError("Failed to read archive entry header while checking directory path");
    }

    auto entry_path = path_utils::NormalizeComparableArchivePath(archive_entry_pathname(entry));
    if (auto match = path_utils::MatchDirectoryPath(entry_path, archive_entry_filetype(entry) == AE_IFDIR, target)) {
      return *match;
    }
  }

  return false;
}

} // namespace giga_edge::data_reader
