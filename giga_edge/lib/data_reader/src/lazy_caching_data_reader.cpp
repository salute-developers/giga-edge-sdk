#include "data_reader_common.h"

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <cassert>
#include <functional>
#include <mutex>

namespace giga_edge {
namespace {

class LazyCachingDataReader : public ILazyDataReader, NonCopyable {
public:
  LazyCachingDataReader(std::function<IDataReaderPtr()> data_reader_factory, DataReaderOptions options)
    : data_reader_factory_(std::move(data_reader_factory)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    if (auto cached = Cached(file_path)) {
      return CreateMovableDataSpan(std::move(cached));
    }
    return Base()->ReadTransientData(file_path);
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    if (auto cached = Cached(file_path)) {
      return CreateMovableDataSpan(std::move(cached));
    }
    auto data = Base()->ReadPersistentData(file_path);
    std::scoped_lock lock(mutex_);
    cache_[file_path] = data;
    return data;
  }

  bool Exists(const std::filesystem::path& path) override {
    const auto cache_key = data_reader::path_utils::NormalizeReaderPath(path);
    if (cache_key.empty()) {
      return true;
    }
    if (Cached(cache_key)) {
      return true;
    }
    return Base()->Exists(path);
  }

  bool IsDirectory(const std::filesystem::path& path) override {
    return Base()->IsDirectory(path);
  }

  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  ) override {
    return Base()->ListFiles(directory_path, std::move(extension));
  }

  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path) override {
    return Base()->ListDirectories(directory_path);
  }

  void Free() override {
    std::scoped_lock lock(mutex_);
    base_reader_.reset();
  }

  std::optional<std::string> GetPathPrefix() const override {
    const auto base = Current();
    return base ? base->GetPathPrefix() : std::nullopt;
  }

  std::optional<ArchiveMemberLocation> GetArchiveMemberLocation(const std::string& file_path) const override {
    const auto base = Current();
    return base ? base->GetArchiveMemberLocation(file_path) : std::nullopt;
  }

private:
  IDataSpanPtr Cached(const std::string& file_path) {
    std::scoped_lock lock(mutex_);
    auto found = cache_.find(file_path);
    if (found == cache_.end()) {
      return nullptr;
    }
    auto span = found->second.lock();
    if (!span) {
      cache_.erase(found);
    }
    return span;
  }

  IDataReaderPtr Base() {
    std::scoped_lock lock(mutex_);
    if (!base_reader_) {
      base_reader_ = data_reader_factory_();
    }
    if (!base_reader_) {
      throw ReaderException("Data reader factory returned no reader");
    }
    return base_reader_;
  }

  IDataReaderPtr Current() const {
    std::scoped_lock lock(mutex_);
    return base_reader_;
  }

  std::function<IDataReaderPtr()> data_reader_factory_;
  mutable IDataReaderPtr base_reader_;

  std::unordered_map<std::string, std::weak_ptr<IDataSpan>> cache_;

  mutable std::mutex mutex_;
};
} // namespace

ILazyDataReaderPtr CreateLazyCachingDataReader(std::function<IDataReaderPtr()> factory, DataReaderOptions options) {
  return std::make_shared<LazyCachingDataReader>(std::move(factory), options);
}

} // namespace giga_edge
