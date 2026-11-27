#pragma once

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/span.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace giga_edge {
namespace data_reader {

// Owns a read-only memory mapping of a file range; the mapping is released in the destructor.
class MemoryMappedDataSpan : public IDataSpan, NonCopyable {
public:
  MemoryMappedDataSpan(void* base, size_t mapped_size, size_t delta, size_t span_size);

  ~MemoryMappedDataSpan() override;

  std::pair<const char*, size_t> Get() override;

private:
  void* base_;
  [[maybe_unused]] size_t mapped_size_;
  const char* span_ptr_;
  size_t span_size_;
};

// Keeps a file open for repeated read-only mappings of its ranges.
class MappedFile : NonCopyable {
public:
  explicit MappedFile(const std::string& path);

  ~MappedFile();

  // The returned span owns only the mapping, not the file handle: POSIX keeps a mapping valid after close(2), and
  // on Windows a mapped view keeps the file section alive after the handles are closed, so the span stays valid
  // even once this object is destroyed.
  IDataSpanPtr Map(uint64_t offset, size_t size) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace data_reader
} // namespace giga_edge
