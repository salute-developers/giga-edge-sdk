#include "file_archive_common.h"
#include "memory_mapped_file.h"

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/file_path.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace giga_edge {

namespace {

class MemoryMappedTarDataReader : public IDataReader, NonCopyable {
public:
  explicit MemoryMappedTarDataReader(std::string&& archive_path)
    : archive_path_(std::move(archive_path)) {
    const auto archive = FilePath(archive_path_);
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive, error)) {
      throw ReaderException("Tar file " + archive_path_ + " is not a regular file");
    }
    archive_size_ = std::filesystem::file_size(archive, error);
    if (error) {
      throw ReaderException("Failed to stat tar file path " + archive_path_ + ": " + error.message());
    }

    MakeArchiveHelper(); // check if the archive is valid

    file_ = std::make_unique<data_reader::MappedFile>(archive_path_);
  }

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    return Create<IMovableDataSpanPtr>(MakeArchiveHelper().ReadData(file_path));
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    auto arc = MakeArchiveHelper();
    auto [entry_size, entry_position] = arc.SeekEntry(file_path, /*reject_sparse=*/true);
    if (entry_position == 0) {
      return nullptr;
    }

    const auto position = static_cast<uint64_t>(entry_position);
    const auto size = static_cast<uint64_t>(entry_size);
    if ((archive_size_ < position) || ((archive_size_ - position) < size)) {
      throw ReaderException("Entry invariant violated for " + file_path + " in archive " + archive_path_);
    }
    if (size == 0) {
      return Create<IDataSpanPtr>(std::vector<char>{});
    }

    return file_->Map(position, static_cast<size_t>(size));
  }

  std::optional<ArchiveMemberLocation> GetArchiveMemberLocation(const std::string& file_path) const override {
    auto arc = MakeArchiveHelper();
    auto [entry_size, entry_position] = arc.SeekEntry(file_path, /*reject_sparse=*/true);
    if (entry_position == 0) {
      return std::nullopt;
    }

    return ArchiveMemberLocation{
      archive_path_,
      static_cast<size_t>(entry_position),
      static_cast<size_t>(entry_size),
    };
  }

  std::vector<std::filesystem::path> ListFiles(
    const std::filesystem::path& directory_path, std::optional<std::string> extension = std::nullopt
  ) override {
    return MakeArchiveHelper().ListFiles(directory_path, std::move(extension));
  }

  std::vector<std::filesystem::path> ListDirectories(const std::filesystem::path& directory_path) override {
    return MakeArchiveHelper().ListDirectories(directory_path);
  }

  bool Exists(const std::filesystem::path& path) override {
    return MakeArchiveHelper().Exists(path);
  }

  bool IsDirectory(const std::filesystem::path& path) override {
    return MakeArchiveHelper().IsDirectory(path);
  }

private:
  data_reader::FileArchiveHelper MakeArchiveHelper() const {
    return data_reader::FileArchiveHelper(archive_path_, false /* no gzip */); // mandatory c++17 copy elision
  }

private:
  std::string archive_path_;
  uint64_t archive_size_ = 0;
  std::unique_ptr<data_reader::MappedFile> file_;
};

} // namespace

IDataReaderPtr CreateMemoryMappedTarDataReader(std::string archive_path, DataReaderOptions options) {
  return std::make_shared<MemoryMappedTarDataReader>(std::move(archive_path));
}

} // namespace giga_edge
