#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

namespace giga_edge {

IDataReaderPtr CreateDataReader(std::string path, DataReaderOptions options) {
  if (IsDirectory(path)) {
    return CreateMemoryMappedDirectoryDataReader(std::move(path), options);
  } else {
    if (IsUncompressedTarArchive(path)) {
      return CreateMemoryMappedTarDataReader(std::move(path), options);
    }
    return CreateArchiveDataReader(std::move(path), options);
  }
}

} // namespace giga_edge
