#include "giga_edge/lib/data_reader/data_reader.h"

namespace giga_edge {

IDataReaderPtr CreateArchiveDataReader(
  [[maybe_unused]] std::string archive_path, [[maybe_unused]] DataReaderOptions options
) {
  throw std::runtime_error("ArchiveDataReader is unsupported");
}

IDataReaderPtr CreatePersistentTarBufferDataReader(
  [[maybe_unused]] IDataSpanPtr span, [[maybe_unused]] DataReaderOptions options
) {
  throw std::runtime_error("InMemoryTarDataReader is unsupported");
}

IDataReaderPtr CreateTemporaryTarBufferDataReader(
  [[maybe_unused]] const char* data, [[maybe_unused]] size_t size, [[maybe_unused]] DataReaderOptions options
) {
  throw std::runtime_error("InMemoryTarDataReader is unsupported");
}

IDataReaderPtr CreateMemoryMappedTarDataReader(
  [[maybe_unused]] std::string archive_path, [[maybe_unused]] DataReaderOptions options
) {
  throw std::runtime_error("MemoryMappedTarDataReader is unsupported");
}

bool IsUncompressedTarArchive([[maybe_unused]] const std::string& archive_path) {
  return false;
}

bool IsTarArchive([[maybe_unused]] const std::string& archive_path) {
  return false;
}

bool IsTarArchive([[maybe_unused]] const char* data, [[maybe_unused]] size_t size) {
  return false;
}

} // namespace giga_edge
