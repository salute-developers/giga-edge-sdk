#include "data_reader_common.h"
#include "directory_reader.h"
#include "memory_mapped_file.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace {

class MemoryMappedDirectoryDataReader : public DirectoryDataReader {
public:
  using DirectoryDataReader::DirectoryDataReader;

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    const auto size = data_reader::RegularFileSize(directory_ + file_path);
    if (!size.has_value()) {
      return nullptr;
    }
    if (*size == 0) {
      // Mapping a zero-length range fails on both platforms.
      return Create<IDataSpanPtr>(std::vector<char>{});
    }

    // The span owns the mapping, the file handle closes at the end of this call.
    const data_reader::MappedFile mapped(directory_ + file_path);
    return mapped.Map(0, static_cast<size_t>(*size));
  }
};

} // namespace

IDataReaderPtr CreateMemoryMappedDirectoryDataReader(std::string directory_path, DataReaderOptions options) {
  return std::make_shared<MemoryMappedDirectoryDataReader>(std::move(directory_path));
}

} // namespace giga_edge
