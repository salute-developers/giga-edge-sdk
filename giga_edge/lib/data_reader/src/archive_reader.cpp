#include "file_archive_common.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace giga_edge {
namespace {

class ArchiveDataReader : public IDataReader {
public:
  explicit ArchiveDataReader(std::string&& archive_path)
    : archive_path_(std::move(archive_path)) {
    MakeArchiveHelper(); // check if the archive is valid
  }

  IMovableDataSpanPtr ReadTransientData(const std::string& file_path) override {
    return Create<IMovableDataSpanPtr>(MakeArchiveHelper().ReadData(file_path));
  }

  IDataSpanPtr ReadPersistentData(const std::string& file_path) override {
    return Create<IDataSpanPtr>(MakeArchiveHelper().ReadData(file_path));
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
    return data_reader::FileArchiveHelper(archive_path_, true /* add gzip */); // mandatory c++17 copy elision
  }

private:
  std::string archive_path_;
};

} // namespace

IDataReaderPtr CreateArchiveDataReader(std::string archive_path, DataReaderOptions options) {
  return std::make_shared<ArchiveDataReader>(std::move(archive_path));
}

} // namespace giga_edge
