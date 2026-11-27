#include "archive.h"
#include "archive_common.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

class InMemoryArchiveHelper : public data_reader::BaseTarArchiveHelper {
public:
  explicit InMemoryArchiveHelper(std::pair<const char*, size_t> data)
    : data_(std::move(data)) {
    if (archive_read_open_memory(arc_, data_.first, data_.second) != ARCHIVE_OK) {
      ThrowArchiveError("Cannot open in-memory tar archive");
    }
  }

  // One-shot.
  std::optional<std::pair<const char*, size_t>> ReadData(const std::string& file_path) {
    auto [entry_size, entry_position] = SeekEntry(file_path, /*reject_sparse=*/true);
    if (entry_position == 0) {
      return std::nullopt;
    }

    const auto position = static_cast<size_t>(entry_position);
    const auto size = static_cast<size_t>(entry_size);
    if (data_.second < position || (data_.second - position) < size) {
      throw ReaderException("Entry invariant violated for " + file_path + " in in-memory tar archive");
    }

    return std::make_pair(data_.first + position, size);
  }


private:
  std::pair<const char*, size_t> data_;
};

class DataSubSpan : public IMovableDataSpan {
public:
  DataSubSpan(std::pair<const char*, size_t>&& data, IDataSpanPtr span)
    : data_(std::move(data))
    , span_(std::move(span)) {}

  std::pair<const char*, size_t> Get() override {
    return data_;
  }

  std::vector<char> Extract() override {
    return std::vector<char>(data_.first, data_.first + data_.second);
  }

private:
  std::pair<const char*, size_t> data_;
  const IDataSpanPtr span_;
};

class PersistentTarBufferDataReader : public IDataReader, NonCopyable {
public:
  explicit PersistentTarBufferDataReader(IDataSpanPtr&& span)
    : span_(std::move(span)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    return MakeFromOptional<IMovableDataSpanPtr, DataSubSpan>(ReadData(file_path), span_);
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    return MakeFromOptional<IDataSpanPtr, DataSubSpan>(ReadData(file_path), span_);
  }

  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  ) override {
    return InMemoryArchiveHelper(span_->Get()).ListFiles(directory_path, std::move(extension));
  }

  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path) override {
    return InMemoryArchiveHelper(span_->Get()).ListDirectories(directory_path);
  }

  bool Exists(const std::filesystem::path& path) override {
    return InMemoryArchiveHelper(span_->Get()).Exists(path);
  }

  bool IsDirectory(const std::filesystem::path& path) override {
    return InMemoryArchiveHelper(span_->Get()).IsDirectory(path);
  }

private:
  std::optional<std::pair<const char*, size_t>> ReadData(const std::string& file_path) {
    return InMemoryArchiveHelper(span_->Get()).ReadData(file_path);
  }

private:
  IDataSpanPtr span_;
};

class TemporaryTarBufferDataReader
  : public IDataReader,
    public std::enable_shared_from_this<TemporaryTarBufferDataReader>,
    NonCopyable {
public:
  explicit TemporaryTarBufferDataReader(std::pair<const char*, size_t> span)
    : span_(std::move(span)) {}

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    if (auto data = ReadData(file_path)) {
      return CreateMovableTemporaryDataSpan(data->first, data->second, weak_from_this());
    }
    return nullptr;
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    if (auto data = ReadData(file_path)) {
      return Create<IDataSpanPtr>(std::vector<char>(data->first, data->first + data->second));
    }
    return nullptr;
  }

  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  ) override {
    return InMemoryArchiveHelper(span_).ListFiles(directory_path, std::move(extension));
  }

  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path) override {
    return InMemoryArchiveHelper(span_).ListDirectories(directory_path);
  }

  bool Exists(const std::filesystem::path& path) override {
    return InMemoryArchiveHelper(span_).Exists(path);
  }

  bool IsDirectory(const std::filesystem::path& path) override {
    return InMemoryArchiveHelper(span_).IsDirectory(path);
  }

private:
  std::optional<std::pair<const char*, size_t>> ReadData(const std::string& file_path) {
    return InMemoryArchiveHelper(span_).ReadData(file_path);
  }

private:
  std::pair<const char*, size_t> span_;
};

} // namespace

IDataReaderPtr CreatePersistentTarBufferDataReader(IDataSpanPtr span, DataReaderOptions options) {
  return std::make_shared<PersistentTarBufferDataReader>(EnsureNotNull(std::move(span)));
}

IDataReaderPtr CreateTemporaryTarBufferDataReader(const char* data, size_t size, DataReaderOptions options) {
  return std::make_shared<TemporaryTarBufferDataReader>(std::make_pair(data, size));
}

bool IsTarArchive(const char* data, size_t size) {
  try {
    InMemoryArchiveHelper({data, size});
  } catch (const ReaderException& exception) {
    return false;
  }
  return true;
}

} // namespace giga_edge
