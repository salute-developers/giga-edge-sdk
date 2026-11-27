#pragma once

#include "exceptions.h"
#include "options.h"
#include "span.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stddef.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {

struct ArchiveMemberLocation {
  std::string archive_path;
  size_t offset;
  size_t size;
};

class IDataReader {
public:
  // The user is advised to release IMovableDataSpanPtr as soon as the data is no more needed.
  // To provide the widest range of applications without loss of efficiency, the returned IMovableDataSpan is only
  // valid until its parent IDataReader is destroyed. Invoking IMovableDataSpan methods or accessing span memory range
  // after IDataReader is destroyed is undefined behavior.
  virtual IMovableDataSpanPtr ReadTransientData(const std::string& file_path) = 0;

  // The user is encouraged to hold IDataSpanPtr as long as the data is needed.
  virtual IDataSpanPtr ReadPersistentData(const std::string& file_path) = 0;

  // Returns the path prefix if the reader is directory-based.
  virtual std::optional<std::string> GetPathPrefix() const {
    return std::nullopt;
  }

  // Returns where the file lies inside the backing archive, for engines that must open the
  // data themselves instead of taking a span. Only readers that keep the archive
  // on disk unmodified can answer; everyone else falls back to the regular read path.
  virtual std::optional<ArchiveMemberLocation> GetArchiveMemberLocation(
    [[maybe_unused]] const std::string& file_path
  ) const {
    return std::nullopt;
  }

  // Returns direct children of the provided directory-like prefix.
  // Paths are reader relative and can be passed back to ReadTransientData/ReadPersistentData.
  // Extension can be provided as ".json" or "json".
  virtual std::vector<std::filesystem::path> ListFiles(
    [[maybe_unused]] const std::filesystem::path& directory_path,
    [[maybe_unused]] std::optional<std::string> extension = std::nullopt
  ) {
    throw ReaderException("ListFiles is not supported by this data reader");
  }

  // Direct child directories, reader relative.
  virtual std::vector<std::filesystem::path> ListDirectories(
    [[maybe_unused]] const std::filesystem::path& directory_path
  ) {
    throw ReaderException("ListDirectories is not supported by this data reader");
  }

  // Checks whether a path exists in the reader namespace.
  virtual bool Exists([[maybe_unused]] const std::filesystem::path& path) {
    throw ReaderException("Exists is not supported by this data reader");
  }

  // Checks whether a path is a directory in the reader namespace.
  virtual bool IsDirectory([[maybe_unused]] const std::filesystem::path& path) {
    throw ReaderException("IsDirectory is not supported by this data reader");
  }

  virtual ~IDataReader() = default;
};

using IDataReaderPtr = std::shared_ptr<IDataReader>;

// Creates data reader only if needed and can release it with Free.
class ILazyDataReader : public IDataReader {
public:
  virtual void Free() = 0;
  std::optional<std::string> GetPathPrefix() const override = 0;
};

using ILazyDataReaderPtr = std::shared_ptr<ILazyDataReader>;

// The following factories throw an exception on error.
IDataReaderPtr CreateArchiveDataReader(std::string archive_path, DataReaderOptions options = {});
IDataReaderPtr CreateDirectoryDataReader(std::string directory_path, DataReaderOptions options = {});
IDataReaderPtr CreatePersistentTarBufferDataReader(IDataSpanPtr span, DataReaderOptions options = {});
IDataReaderPtr CreateTemporaryTarBufferDataReader(const char* data, size_t size, DataReaderOptions options = {});
IDataReaderPtr CreateMemoryMappedTarDataReader(std::string archive_path, DataReaderOptions options = {});
IDataReaderPtr CreateMemoryMappedDirectoryDataReader(std::string directory_path, DataReaderOptions options = {});

IDataReaderPtr CreateDataReader(std::string path, DataReaderOptions options = {});

ILazyDataReaderPtr CreateLazyCachingDataReader(std::function<IDataReaderPtr()> factory, DataReaderOptions options = {});

bool IsUncompressedTarArchive(const std::string& archive_path);

bool IsTarArchive(const std::string& archive_path);
bool IsTarArchive(const char* data, size_t size);

} // namespace giga_edge
