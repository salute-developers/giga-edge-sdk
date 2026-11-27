#include "memory_mapped_file.h"

#include "giga_edge/lib/data_reader/file_path.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <iostream>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace giga_edge {
namespace data_reader {

MemoryMappedDataSpan::MemoryMappedDataSpan(void* base, size_t mapped_size, size_t delta, size_t span_size)
  : base_(base)
  , mapped_size_(mapped_size)
  , span_ptr_(static_cast<const char*>(base) + delta)
  , span_size_(span_size) {}

MemoryMappedDataSpan::~MemoryMappedDataSpan() {
#if defined(_WIN32)
  ::UnmapViewOfFile(base_);
#else
  ::munmap(base_, mapped_size_);
#endif
}

std::pair<const char*, size_t> MemoryMappedDataSpan::Get() {
  return {span_ptr_, span_size_};
}

namespace {

#if defined(_WIN32)

std::string LastSystemError() {
  return std::system_category().message(static_cast<int>(::GetLastError()));
}

#else

std::string LastSystemError() {
  return std::generic_category().message(errno);
}

#endif

} // namespace

#if defined(_WIN32)

class MappedFile::Impl {
public:
  explicit Impl(const std::string& path) {
    file_ = ::CreateFileW(
      FilePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file_ == INVALID_HANDLE_VALUE) {
      throw ReaderException("Failed to open file path " + path + ": " + LastSystemError());
    }
    mapping_ = ::CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping_ == nullptr) {
      const auto message = LastSystemError();
      ::CloseHandle(file_);
      throw ReaderException("Failed to map file " + path + ": " + message);
    }
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    granularity_ = info.dwAllocationGranularity;
  }

  ~Impl() {
    ::CloseHandle(mapping_);
    ::CloseHandle(file_);
  }

  IDataSpanPtr Map(uint64_t offset, size_t size) const {
    const uint64_t delta = offset % granularity_;
    const uint64_t base = offset - delta;
    const size_t mapped_size = size + static_cast<size_t>(delta);
    void* addr = ::MapViewOfFile(
      mapping_, FILE_MAP_READ, static_cast<DWORD>(base >> 32), static_cast<DWORD>(base & 0xFFFFFFFFu), mapped_size
    );
    if (addr == nullptr) {
      throw ReaderException("Failed to map file range: " + LastSystemError());
    }
    return Make<IDataSpanPtr, MemoryMappedDataSpan>(addr, mapped_size, static_cast<size_t>(delta), size);
  }

private:
  HANDLE file_ = INVALID_HANDLE_VALUE;
  HANDLE mapping_ = nullptr;
  uint64_t granularity_ = 65536;
};

#else

class MappedFile::Impl {
public:
  explicit Impl(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ == -1) {
      throw ReaderException("Failed to open file path " + path + ": " + LastSystemError());
    }
    // sysconf returns -1 on failure: keep the default granularity then.
    const auto page_size = ::sysconf(_SC_PAGE_SIZE);
    if (page_size > 0) {
      granularity_ = static_cast<uint64_t>(page_size);
    }
  }

  ~Impl() {
    if (::close(fd_) == -1) {
      std::cerr << "ERROR: Failed to close file descriptor: errno = " << errno << '\n';
    }
  }

  IDataSpanPtr Map(uint64_t offset, size_t size) const {
    const uint64_t delta = offset % granularity_;
    const size_t mapped_size = size + static_cast<size_t>(delta);
    void* addr = ::mmap(nullptr, mapped_size, PROT_READ, MAP_SHARED, fd_, static_cast<off_t>(offset - delta));
    if (addr == MAP_FAILED) {
      throw ReaderException("Failed to map file range: " + LastSystemError());
    }
    return Make<IDataSpanPtr, MemoryMappedDataSpan>(addr, mapped_size, static_cast<size_t>(delta), size);
  }

private:
  int fd_ = -1;
  uint64_t granularity_ = 4096;
};

#endif

MappedFile::MappedFile(const std::string& path)
  : impl_(std::make_unique<Impl>(path)) {}

MappedFile::~MappedFile() = default;

IDataSpanPtr MappedFile::Map(uint64_t offset, size_t size) const {
  return impl_->Map(offset, size);
}

} // namespace data_reader
} // namespace giga_edge
