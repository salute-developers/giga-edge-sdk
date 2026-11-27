#include "data_reader_common.h"

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/file_path.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace giga_edge {
namespace {

class VectorDataSpan : public IMovableDataSpan {
public:
  explicit VectorDataSpan(std::vector<char>&& data)
    : data_(std::move(data)) {}

  std::pair<const char*, size_t> Get() override {
    return std::make_pair(data_.data(), data_.size());
  }

  std::vector<char> Extract() override {
    return std::move(data_);
  }

private:
  std::vector<char> data_;
};

class RangeDataSpan : public IMovableDataSpan {
  using WeakType = IDataReaderPtr::weak_type;

public:
  explicit RangeDataSpan(std::pair<const char*, size_t> data, WeakType&& parent = WeakType())
    : data_(std::move(data))
    , parent_(std::move(parent)) {}

  std::pair<const char*, size_t> Get() override {
    Validate();
    return data_;
  }

  std::vector<char> Extract() override {
    Validate();
    return std::vector<char>(data_.first, data_.first + data_.second);
  }

private:
  void Validate() const {
    // Not race-free: catches a span used after its reader is gone in single-threaded code such as tests.
    if ((parent_.owner_before(IDataReaderPtr()) || WeakType().owner_before(parent_)) && parent_.expired()) {
      throw ReaderException("Temporary data span is expired");
    }
  }

private:
  std::pair<const char*, size_t> data_;
  const WeakType parent_;
};

class MovableDataSpanAdapter : public IMovableDataSpan {
public:
  explicit MovableDataSpanAdapter(IDataSpanPtr&& span)
    : span_(std::move(span)) {}

  std::pair<const char*, size_t> Get() override {
    return span_->Get();
  }

  std::vector<char> Extract() override {
    auto data = span_->Get();
    return std::vector<char>(data.first, data.first + data.second);
  }

private:
  IDataSpanPtr span_;
};

class CachingDataReader : public IDataReader, NonCopyable {
public:
  CachingDataReader(IDataReaderPtr&& base_reader, std::unordered_map<std::string, IDataSpanPtr>&& cache)
    : cache_(std::move(cache))
    , base_reader_(std::move(base_reader)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    {
      std::unique_lock lock(mutex_);
      auto found = cache_.find(file_path);
      if (found != cache_.end()) {
        auto span = found->second;
        lock.unlock();
        return CreateMovableDataSpan(std::move(span));
      }
    }

    if (base_reader_) {
      return base_reader_->ReadTransientData(file_path);
    }
    return nullptr;
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    std::scoped_lock lock(mutex_);

    auto found = cache_.find(file_path);
    if (found == cache_.end()) {
      if (!base_reader_) {
        return nullptr;
      }

      found = cache_.emplace(file_path, base_reader_->ReadPersistentData(file_path)).first;
    }

    return found->second;
  }

  bool Exists(const std::filesystem::path& path) override {
    const auto cache_key = data_reader::path_utils::NormalizeReaderPath(path);
    if (cache_key.empty()) {
      return true;
    }

    {
      std::scoped_lock lock(mutex_);
      if (cache_.find(cache_key) != cache_.end()) {
        return true;
      }
    }

    if (base_reader_) {
      return base_reader_->Exists(path);
    }

    return false;
  }

  bool IsDirectory(const std::filesystem::path& path) override {
    if (base_reader_) {
      return base_reader_->IsDirectory(path);
    }

    return false;
  }

  std::optional<std::string> GetPathPrefix() const override {
    return base_reader_ ? base_reader_->GetPathPrefix() : std::nullopt;
  }

  std::optional<ArchiveMemberLocation> GetArchiveMemberLocation(const std::string& file_path) const override {
    return base_reader_ ? base_reader_->GetArchiveMemberLocation(file_path) : std::nullopt;
  }

  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& path, std::optional<std::string> extension = std::nullopt
  ) override {
    if (!base_reader_) {
      throw ReaderException("No directory source for: " + path.string());
    }
    return base_reader_->ListFiles(path, std::move(extension));
  }

  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& path) override {
    if (!base_reader_) {
      throw ReaderException("No directory source for: " + path.string());
    }
    return base_reader_->ListDirectories(path);
  }

private:
  std::unordered_map<std::string, IDataSpanPtr> cache_;
  IDataReaderPtr base_reader_;
  std::mutex mutex_;
};

class AnyPathSameSpanDataReader : public IDataReader, NonCopyable {
public:
  explicit AnyPathSameSpanDataReader(IDataSpanPtr&& span)
    : span_(std::move(span)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string&) override {
    return CreateMovableDataSpan(span_);
  }

  IDataSpanPtr ReadPersistentData(const std::string&) override {
    return span_;
  }

  bool Exists(const std::filesystem::path&) override {
    return true;
  }

  bool IsDirectory(const std::filesystem::path&) override {
    return false;
  }

private:
  IDataSpanPtr span_;
};

class AnyPathSameSpanTemporaryDataReader
  : public IDataReader,
    public std::enable_shared_from_this<AnyPathSameSpanTemporaryDataReader>,
    NonCopyable {
public:
  explicit AnyPathSameSpanTemporaryDataReader(std::pair<const char*, size_t> data)
    : data_(std::move(data)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string&) override {
    return Make<IMovableDataSpanPtr, RangeDataSpan>(data_, weak_from_this());
  }

  IDataSpanPtr ReadPersistentData(const std::string&) override {
    return Make<IDataSpanPtr, VectorDataSpan>(std::vector<char>(data_.first, data_.first + data_.second));
  }

  bool Exists(const std::filesystem::path&) override {
    return true;
  }

  bool IsDirectory(const std::filesystem::path&) override {
    return false;
  }

private:
  std::pair<const char*, size_t> data_;
};

} // namespace

IMovableDataSpanPtr CreateMovableDataSpan(IDataSpanPtr span) {
  return Make<IMovableDataSpanPtr, MovableDataSpanAdapter>(std::move(span));
}

IMovableDataSpanPtr CreateMovableTemporaryDataSpan(const char* data, size_t size, IDataReaderPtr::weak_type parent) {
  return Make<IMovableDataSpanPtr, RangeDataSpan>(std::make_pair(data, size), std::move(parent));
}

IDataReaderPtr CreateCachingDataReader(
  IDataReaderPtr base_reader, std::unordered_map<std::string, IDataSpanPtr> initial_cache
) {
  return std::make_shared<CachingDataReader>(std::move(base_reader), std::move(initial_cache));
}

IDataReaderPtr CreateAnyPathSameSpanDataReader(IDataSpanPtr span) {
  return std::make_shared<AnyPathSameSpanDataReader>(std::move(span));
}

IDataReaderPtr CreateAnyPathSameSpanTemporaryDataReader(const char* data, size_t size) {
  return std::make_shared<AnyPathSameSpanTemporaryDataReader>(std::make_pair(data, size));
}

std::string ToString(IDataSpan& span) {
  auto data = span.Get();
  return std::string(data.first, data.second);
}

bool IsDirectory(const std::string& path) {
  std::error_code error;
  return std::filesystem::is_directory(FilePath(path), error);
}

namespace data_reader {

std::optional<uintmax_t> RegularFileSize(const std::string& file_path) {
  const auto file = FilePath(file_path);
  std::error_code error;
  if (!std::filesystem::is_regular_file(file, error)) {
    return std::nullopt;
  }

  const auto size = std::filesystem::file_size(file, error);
  if (error) {
    throw ReaderException("Failed to stat file " + file_path + ": " + error.message());
  }

  return size;
}

} // namespace data_reader

std::optional<std::vector<char>> ReadFile(const std::string& file_path) {
  const auto size = data_reader::RegularFileSize(file_path);
  if (!size.has_value()) {
    return std::nullopt;
  }

  std::ifstream fin(FilePath(file_path), std::ios::binary);
  std::vector<char> result(static_cast<size_t>(*size));
  fin.read(result.data(), result.size());
  if (fin.gcount() != result.size()) {
    throw ReaderException("Failed to read the entire file " + file_path);
  }

  return result;
}

namespace data_reader {

template<typename T>
detail::IfSpanPtr<T> Create(std::vector<char> data) {
  return Make<T, VectorDataSpan>(std::move(data));
}

template<typename T>
detail::IfSpanPtr<T> Create(std::pair<const char*, size_t> data) {
  return Make<T, RangeDataSpan>(std::move(data));
}

template IDataSpanPtr Create<IDataSpanPtr>(std::vector<char> data);
template IMovableDataSpanPtr Create<IMovableDataSpanPtr>(std::vector<char> data);
template IDataSpanPtr Create<IDataSpanPtr>(std::pair<const char*, size_t> data);
template IMovableDataSpanPtr Create<IMovableDataSpanPtr>(std::pair<const char*, size_t> data);

} // namespace data_reader

} // namespace giga_edge
