#include "giga_edge/lib/data_reader/data_reader.h"

#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

void TestReader(giga_edge::IDataReaderPtr reader) {
  auto test_reader = [](auto&& read_data) {
    REQUIRE(ToString(read_data("test1.txt")) == "Test 1");
    REQUIRE(ToString(read_data("test2.txt")) == "Test 2");
    REQUIRE(ToString(read_data("folder/test3.txt")) == "Test 3");
    REQUIRE_FALSE(read_data("test3.txt"));
    REQUIRE_FALSE(read_data("folder"));
  };

  using namespace std::placeholders;

  SECTION("Transient data read") {
    test_reader(std::bind(&giga_edge::IDataReader::ReadTransientData, reader.get(), _1));
  }

  SECTION("Persistent data read") {
    test_reader(std::bind(&giga_edge::IDataReader::ReadPersistentData, reader.get(), _1));
  }
}

void TestExistsAndIsDirectory(giga_edge::IDataReaderPtr reader) {
  REQUIRE(reader->Exists(""));
  REQUIRE(reader->IsDirectory(""));

  REQUIRE(reader->Exists("test1.txt"));
  REQUIRE_FALSE(reader->IsDirectory("test1.txt"));

  REQUIRE(reader->Exists("folder"));
  REQUIRE(reader->IsDirectory("folder"));

  REQUIRE(reader->Exists("folder/"));
  REQUIRE(reader->IsDirectory("folder/"));

  REQUIRE_FALSE(reader->Exists("missing.file"));
  REQUIRE_FALSE(reader->IsDirectory("missing.file"));
}

namespace {

// Minimal ustar writer: just enough structure for the in-memory reader tests.
void PutOctal(char* header, size_t offset, size_t width, uint64_t value) {
  std::snprintf(header + offset, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(value));
}

void WriteTarChecksum(std::vector<char>& header) {
  std::fill(header.begin() + 148, header.begin() + 156, ' ');
  unsigned checksum = 0;
  for (const char byte : header) {
    checksum += static_cast<unsigned char>(byte);
  }
  std::snprintf(header.data() + 148, 8, "%06o", checksum);
  header[154] = '\0';
  header[155] = ' ';
}

void AppendTarEntry(std::vector<char>& buffer, const std::string& name, const std::string& content) {
  std::vector<char> header(512, '\0');
  std::snprintf(header.data(), 100, "%s", name.c_str());
  PutOctal(header.data(), 100, 8, 0644); // mode
  PutOctal(header.data(), 108, 8, 0);    // uid
  PutOctal(header.data(), 116, 8, 0);    // gid
  PutOctal(header.data(), 124, 12, content.size());
  PutOctal(header.data(), 136, 12, 0); // mtime
  header[156] = '0';                   // regular file
  std::memcpy(header.data() + 257, "ustar\00000", 8);
  WriteTarChecksum(header);

  buffer.insert(buffer.end(), header.begin(), header.end());
  buffer.insert(buffer.end(), content.begin(), content.end());
  buffer.resize((buffer.size() + 511) / 512 * 512, '\0');
}

std::vector<char> MakeTarBuffer(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::vector<char> buffer;
  for (const auto& [name, content] : entries) {
    AppendTarEntry(buffer, name, content);
  }

  buffer.resize(buffer.size() + 1024, '\0');
  return buffer;
}

void AppendTarHardlink(std::vector<char>& buffer, const std::string& name, const std::string& target) {
  std::vector<char> header(512, '\0');
  std::snprintf(header.data(), 100, "%s", name.c_str());
  PutOctal(header.data(), 100, 8, 0644); // mode
  PutOctal(header.data(), 108, 8, 0);    // uid
  PutOctal(header.data(), 116, 8, 0);    // gid
  PutOctal(header.data(), 124, 12, 0);   // size: a hardlink carries no data of its own
  PutOctal(header.data(), 136, 12, 0);   // mtime
  header[156] = '1';                     // hardlink
  std::snprintf(header.data() + 157, 100, "%s", target.c_str());
  std::memcpy(header.data() + 257, "ustar\00000", 8);
  WriteTarChecksum(header);

  buffer.insert(buffer.end(), header.begin(), header.end());
  buffer.resize((buffer.size() + 511) / 512 * 512, '\0');
}

struct TempTarFile {
  std::filesystem::path path;

  explicit TempTarFile(const std::vector<char>& buffer)
    : path(
        std::filesystem::temp_directory_path()
        / ("giga_edge_sparse_tar_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tar")
      ) {
    std::ofstream out(path, std::ios::binary);
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out.close();
    if (!out) {
      throw std::runtime_error("failed to write " + path.string());
    }
  }

  ~TempTarFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

// GNU old-style sparse entry: realsize=1024 (the logical size), only the non-empty chunk
// "12345678" is stored (chunks are not padded, the size field holds the sum of chunk bytes),
// the chunk map lives in the header fields. The next.txt entry after it occupies the area that
// a direct [offset, logical size) mapping would cross into without the guard.
std::vector<char> MakeSparseTarBuffer() {
  std::vector<char> buffer(512, '\0');
  char* const header = buffer.data();

  std::snprintf(header, 100, "%s", "blob.bin");
  PutOctal(header, 100, 8, 0644);            // mode
  PutOctal(header, 108, 8, 0);               // uid
  PutOctal(header, 116, 8, 0);               // gid
  PutOctal(header, 124, 12, 8);              // size: on-disk chunk bytes
  PutOctal(header, 136, 12, 0);              // mtime
  header[156] = 'S';                         // GNU sparse file
  std::memcpy(header + 257, "ustar  \0", 8); // GNU magic: the entry is parsed as gnutar
  PutOctal(header, 386, 12, 0);              // sparse[0].offset
  PutOctal(header, 398, 12, 8);              // sparse[0].numbytes
  header[482] = '\0';                        // isextended: no map continuation
  PutOctal(header, 483, 12, 1024);           // realsize: the logical entry size
  WriteTarChecksum(buffer);

  buffer.resize(1024, '\0');
  std::memcpy(buffer.data() + 512, "12345678", 8); // stored chunk bytes + block padding

  AppendTarEntry(buffer, "next.txt", "NEXT");
  buffer.resize(buffer.size() + 1024, '\0'); // end-of-archive marker
  return buffer;
}

} // namespace

TEST_CASE("test in-memory tar reader entry bounds", "[util][data_reader]") {
  using namespace giga_edge;

  SECTION("Empty file yields an empty span") {
    const auto buffer = MakeTarBuffer({{"empty.txt", ""}, {"data.txt", "Test 1"}});
    auto reader = CreateTemporaryTarBufferDataReader(buffer.data(), buffer.size());

    const auto empty_span = EnsureNotNull(reader->ReadPersistentData("empty.txt"));
    REQUIRE(empty_span->Get().second == 0);
    REQUIRE(ToString(EnsureNotNull(reader->ReadPersistentData("data.txt"))) == "Test 1");
  }

  SECTION("Entry truncated by the buffer is rejected") {
    const auto buffer = MakeTarBuffer({{"big.txt", std::string(1000, 'x')}});
    const auto reader = CreateTemporaryTarBufferDataReader(buffer.data(), 512 + 600);

    REQUIRE_THROWS_AS(reader->ReadPersistentData("big.txt"), ReaderException);
  }
}

TEST_CASE("tar readers do not match hardlink entries", "[util][data_reader]") {
  using namespace giga_edge;

  std::vector<char> buffer;
  AppendTarEntry(buffer, "data.txt", "Test 1");
  AppendTarHardlink(buffer, "alias.txt", "data.txt");
  buffer.resize(buffer.size() + 1024, '\0'); // end-of-archive marker

  SECTION("in-memory reader reads a hardlink as absent") {
    auto reader = CreateTemporaryTarBufferDataReader(buffer.data(), buffer.size());
    REQUIRE_FALSE(reader->ReadPersistentData("alias.txt"));
    REQUIRE(ToString(EnsureNotNull(reader->ReadPersistentData("data.txt"))) == "Test 1");
  }

  SECTION("archive reader is consistent") {
    const TempTarFile tar_file(buffer);
    auto reader = CreateArchiveDataReader(tar_file.path.string());
    REQUIRE_FALSE(reader->ReadPersistentData("alias.txt"));
    REQUIRE(ToString(EnsureNotNull(reader->ReadPersistentData("data.txt"))) == "Test 1");
  }
}

TEST_CASE("test archive reader", "[util][data_reader]") {
  SECTION("Reading tar archive") {
    TestReader(giga_edge::CreateArchiveDataReader("data/test.tar"));
  }

  SECTION("Reading unsupported archive should fail") {
    REQUIRE_THROWS_AS(giga_edge::CreateArchiveDataReader("data/test.zip"), giga_edge::ReaderException);
  }

  SECTION("Reading nonexisted archive should fail") {
    REQUIRE_THROWS_AS(giga_edge::CreateArchiveDataReader("data/nonexisting"), giga_edge::ReaderException);
  }

  SECTION("List files in root and direct subfolder") {
    auto reader = giga_edge::CreateArchiveDataReader("data/test.tar");

    auto root = reader->ListFiles("");
    REQUIRE(root == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    auto folder = reader->ListFiles("folder");
    REQUIRE(folder == std::vector<std::filesystem::path>{"folder/test3.txt"});
  }

  SECTION("List files with extesion filter") {
    auto reader = giga_edge::CreateArchiveDataReader("data/test.tar");

    REQUIRE(reader->ListFiles("", "txt") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("folder", ".txt") == std::vector<std::filesystem::path>{"folder/test3.txt"});
    REQUIRE(reader->ListFiles("", ".json").empty());
  }

  SECTION("List directories in root and below") {
    auto reader = giga_edge::CreateArchiveDataReader("data/test.tar");

    REQUIRE(reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"});
    REQUIRE(reader->ListDirectories("folder").empty());
  }
}

TEST_CASE("test directory reader", "[util][data_reader]") {
  auto reader = giga_edge::CreateDirectoryDataReader("data/test/");
  TestReader(reader);

  auto root = reader->ListFiles("");
  REQUIRE(root == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
  auto folder = reader->ListFiles("folder");
  REQUIRE(folder == std::vector<std::filesystem::path>{"folder/test3.txt"});

  REQUIRE(reader->ListFiles("folder", ".txt") == std::vector<std::filesystem::path>{"folder/test3.txt"});
  REQUIRE(reader->ListFiles("", ".json").empty());

  REQUIRE(reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"});
  REQUIRE(reader->ListDirectories("folder").empty());
}

TEST_CASE("test memory mapped directory data reader", "[util][data_reader]") {
  SECTION("Reading directory") {
    TestReader(giga_edge::CreateMemoryMappedDirectoryDataReader("data/test/"));
  }

  SECTION("Nonexistent directory should fail") {
    REQUIRE_THROWS_AS(giga_edge::CreateMemoryMappedDirectoryDataReader("data/nonexisting"), giga_edge::ReaderException);
  }

  SECTION("Persistent span outlives the reader") {
    auto reader = giga_edge::CreateMemoryMappedDirectoryDataReader("data/test/");
    auto data = giga_edge::EnsureNotNull(reader->ReadPersistentData("test2.txt"));
    reader.reset();
    REQUIRE(giga_edge::ToString(*data) == "Test 2");
  }

  SECTION("Path prefix matches the plain directory reader") {
    auto mapped = giga_edge::CreateMemoryMappedDirectoryDataReader("data/test/");
    auto plain = giga_edge::CreateDirectoryDataReader("data/test/");
    REQUIRE(*mapped->GetPathPrefix() == *plain->GetPathPrefix());
  }

  SECTION("List files in root and direct subfolder") {
    auto reader = giga_edge::CreateMemoryMappedDirectoryDataReader("data/test/");

    REQUIRE(reader->ListFiles("") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("folder") == std::vector<std::filesystem::path>{"folder/test3.txt"});
    REQUIRE(reader->ListFiles("", ".txt") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("", ".json").empty());

    REQUIRE(reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"});
    REQUIRE(reader->ListDirectories("folder").empty());
  }
}

namespace {

struct TempDirGuard {
  std::filesystem::path path;

  ~TempDirGuard() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

TEST_CASE("test memory mapped directory data reader file sizes", "[util][data_reader]") {
  using namespace giga_edge;

  const auto base = std::filesystem::temp_directory_path() / "giga_edge_mmap_dir_reader_test";
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  const TempDirGuard temp{base};

  {
    std::ofstream(base / "empty.bin", std::ios::binary | std::ios::trunc).close();
  }

  std::vector<char> big(3 * 4096);
  for (size_t i = 0; i < big.size(); ++i) {
    big[i] = static_cast<char>('a' + i % 26);
  }
  {
    std::ofstream out(base / "big.bin", std::ios::binary | std::ios::trunc);
    out.write(big.data(), static_cast<std::streamsize>(big.size()));
  }

  const auto reader = CreateMemoryMappedDirectoryDataReader(base.string());

  SECTION("Empty file maps to an empty persistent span") {
    auto span = EnsureNotNull(reader->ReadPersistentData("empty.bin"));
    REQUIRE(span->Get().second == 0);
  }

  SECTION("Empty file reads as an empty transient span") {
    auto span = EnsureNotNull(reader->ReadTransientData("empty.bin"));
    REQUIRE(span->Get().second == 0);
  }

  SECTION("Multi-page file maps whole content") {
    auto span = EnsureNotNull(reader->ReadPersistentData("big.bin"));
    REQUIRE(span->Get().second == big.size());
    REQUIRE(span->View() == std::string_view(big.data(), big.size()));
  }
}

TEST_CASE("test memory mapped tar data reader", "[util][data_reader]") {
  SECTION("Reading tar archive") {
    TestReader(giga_edge::CreateMemoryMappedTarDataReader("data/test.tar"));
  }

  SECTION("List directories in root and below") {
    auto reader = giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");

    REQUIRE(reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"});
    REQUIRE(reader->ListDirectories("folder").empty());
  }

  SECTION("Member location is still reported through the decoration") {
    auto reader = giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");
    auto location = reader->GetArchiveMemberLocation("test1.txt");
    REQUIRE(location.has_value());
    REQUIRE(location->size == 6);
  }
}

TEST_CASE("contiguous tar readers reject sparse entries", "[util][data_reader]") {
  using namespace giga_edge;
  const auto buffer = MakeSparseTarBuffer();
  const TempTarFile tar_file(buffer);

  SECTION("memory-mapped reader throws instead of mapping across the entry") {
    auto reader = CreateMemoryMappedTarDataReader(tar_file.path.string());
    REQUIRE_THROWS_AS(reader->ReadPersistentData("blob.bin"), ReaderException);
    REQUIRE_THROWS_AS(reader->GetArchiveMemberLocation("blob.bin"), ReaderException);
  }

  SECTION("in-memory buffer readers throw for both read kinds") {
    auto reader = CreatePersistentTarBufferDataReader(Create<IDataSpan>(buffer.data(), buffer.size()));
    REQUIRE_THROWS_AS(reader->ReadTransientData("blob.bin"), ReaderException);
    REQUIRE_THROWS_AS(reader->ReadPersistentData("blob.bin"), ReaderException);
  }

  // Dense entries keep reading through the regular path: archive_read_data fills the holes
  // between chunks with zeros, only the direct [offset, size) windows must reject the entry.
  SECTION("dense entries in the same archive read as usual") {
    auto reader = CreateMemoryMappedTarDataReader(tar_file.path.string());
    REQUIRE(reader->Exists("blob.bin"));
    REQUIRE(reader->ListFiles("") == std::vector<std::filesystem::path>{"blob.bin", "next.txt"});
    REQUIRE(ToString(EnsureNotNull(reader->ReadTransientData("next.txt"))) == "NEXT");
  }
}

TEST_CASE("test persistent memory tar data reader", "[util][data_reader]") {
  using namespace giga_edge;

  auto tar_data = EnsureNotNull(ReadFileTo<IDataSpanPtr>("data/test.tar"));

  SECTION("Reading tar archive") {
    TestReader(CreatePersistentTarBufferDataReader(tar_data));
  }

  SECTION("Checking data persistence") {
    auto reader = CreatePersistentTarBufferDataReader(tar_data);

    tar_data.reset();

    auto persistent_span = EnsureNotNull(reader->ReadPersistentData("test2.txt"));
    auto transient_data = ToString(reader->ReadTransientData("test2.txt"));

    reader.reset();

    REQUIRE(ToString(*persistent_span) == "Test 2");
    REQUIRE(transient_data == "Test 2");
  }

  SECTION("Tar archive data change") {
    auto buf = ToString(tar_data);
    auto reader = CreatePersistentTarBufferDataReader(Create<IDataSpan>(buf.data(), buf.size()));
    {
      auto span = EnsureNotNull(reader->ReadPersistentData("test1.txt"));
      REQUIRE(ToString(*span) == "Test 1");
      auto [data, size] = span->Get();

      auto mutable_data = const_cast<char*>(data);
      for (int i = 0; i < size; ++i) {
        mutable_data[i] = 'a';
      }
    }

    {
      auto span = reader->ReadPersistentData("test1.txt");
      REQUIRE(ToString(span) == "aaaaaa");
    }
  }

  SECTION("List files") {
    auto reader = CreatePersistentTarBufferDataReader(tar_data);

    REQUIRE(reader->ListFiles("") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("folder") == std::vector<std::filesystem::path>{"folder/test3.txt"});
    REQUIRE(reader->ListFiles("", "txt") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
  }
}

TEST_CASE("test temporary memory tar data reader", "[util][data_reader]") {
  using namespace giga_edge;

  auto buf = ReadFile("data/test.tar").value();

  SECTION("Reading tar archive") {
    TestReader(CreateTemporaryTarBufferDataReader(buf.data(), buf.size()));
  }

  SECTION("Checking data persistence") {
    auto reader = CreateTemporaryTarBufferDataReader(buf.data(), buf.size());

    auto persistent_span = EnsureNotNull(reader->ReadPersistentData("test2.txt"));
    auto transient_span = EnsureNotNull(reader->ReadTransientData("test2.txt"));
    auto transient_data = ToString(*transient_span);

    reader.reset();
    std::fill(buf.begin(), buf.end(), '\x5a'); // invalidate data range
    std::vector<char>().swap(buf);

    REQUIRE(ToString(*persistent_span) == "Test 2");
    REQUIRE(transient_data == "Test 2");
    REQUIRE_THROWS_AS(ToString(*transient_span), giga_edge::ReaderException);
  }

  SECTION("List files") {
    auto reader = CreateTemporaryTarBufferDataReader(buf.data(), buf.size());

    REQUIRE(reader->ListFiles("") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("folder") == std::vector<std::filesystem::path>{"folder/test3.txt"});
    REQUIRE(reader->ListFiles("", "txt") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
  }
}

TEST_CASE("test memory mapped tar reader", "[util][data_reader]") {
  SECTION("Reading tar archive") {
    TestReader(giga_edge::CreateMemoryMappedTarDataReader("data/test.tar"));
  }

  SECTION("Reading unsupported archive should fail") {
    REQUIRE_THROWS_AS(giga_edge::CreateMemoryMappedTarDataReader("data/test.tar.gz"), giga_edge::ReaderException);

    REQUIRE_THROWS_AS(giga_edge::CreateMemoryMappedTarDataReader("data/test.zip"), giga_edge::ReaderException);
  }

  SECTION("Reading nonexisted archive should fail") {
    REQUIRE_THROWS_AS(giga_edge::CreateMemoryMappedTarDataReader("data/nonexisting"), giga_edge::ReaderException);
  }

  SECTION("Checking data persistence") {
    auto reader = giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");
    auto data = EnsureNotNull(reader->ReadPersistentData("test2.txt"));
    reader.reset();
    REQUIRE(ToString(*data) == "Test 2");
  }

  SECTION("List files in root and direct subfolder") {
    auto reader = giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");

    auto root = reader->ListFiles("");
    REQUIRE(root == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});

    auto folder = reader->ListFiles("folder");
    REQUIRE(folder == std::vector<std::filesystem::path>{"folder/test3.txt"});
  }

  SECTION("List files with extension filter") {
    auto reader = giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");

    REQUIRE(reader->ListFiles("", "txt") == std::vector<std::filesystem::path>{"test1.txt", "test2.txt"});
    REQUIRE(reader->ListFiles("folder", ".txt") == std::vector<std::filesystem::path>{"folder/test3.txt"});
    REQUIRE(reader->ListFiles("", ".json").empty());
  }
}

TEST_CASE("test any path same span data reader", "[util][data_reader]") {
  using namespace giga_edge;

  const std::string target = "Test span";

  SECTION("Persistent memory buffer") {
    auto reader = giga_edge::CreateAnyPathSameSpanDataReader(
      Create<giga_edge::IDataSpan>(target.data(), target.size())
    );

    auto persistent_span1 = EnsureNotNull(reader->ReadPersistentData(""));
    auto persistent_span2 = EnsureNotNull(reader->ReadPersistentData("<fake path>"));
    auto transient_data1 = ToString(reader->ReadTransientData(""));
    auto transient_data2 = ToString(reader->ReadTransientData("<fake path>"));

    reader.reset();

    REQUIRE(ToString(*persistent_span1) == target);
    REQUIRE(ToString(*persistent_span2) == target);
    REQUIRE(transient_data1 == target);
    REQUIRE(transient_data2 == target);
  }

  SECTION("Temporary memory buffer") {
    std::vector<char> buf(target.begin(), target.end());

    auto reader = giga_edge::CreateAnyPathSameSpanTemporaryDataReader(buf.data(), buf.size());

    auto persistent_span1 = EnsureNotNull(reader->ReadPersistentData(""));
    auto persistent_span2 = EnsureNotNull(reader->ReadPersistentData("<fake path>"));
    auto transient_span1 = EnsureNotNull(reader->ReadTransientData(""));
    auto transient_span2 = EnsureNotNull(reader->ReadTransientData("<fake path>"));
    auto transient_data1 = ToString(*transient_span1);
    auto transient_data2 = ToString(*transient_span2);

    reader.reset();
    std::fill(buf.begin(), buf.end(), '\x5a'); // invalidate data range
    std::vector<char>().swap(buf);

    REQUIRE(ToString(*persistent_span1) == target);
    REQUIRE(ToString(*persistent_span2) == target);
    REQUIRE(transient_data1 == target);
    REQUIRE(transient_data2 == target);
    REQUIRE_THROWS_AS(ToString(*transient_span1), giga_edge::ReaderException);
    REQUIRE_THROWS_AS(ToString(*transient_span2), giga_edge::ReaderException);
  }
}

TEST_CASE("test CreateDataReader dispatcher", "[util][data_reader]") {
  using namespace giga_edge;

  auto test = [](auto f, auto* p) {
    auto dispatched = CreateDataReader(p);
    auto target = f(p, {});

    // Inhibit warning: expression with side effects will be evaluated despite being used as an operand to 'typeid'.
    auto& dref = *dispatched;
    auto& tref = *target;

    REQUIRE(typeid(dref) == typeid(tref));
  };

  SECTION("Directory reader") {
    test(&CreateMemoryMappedDirectoryDataReader, "data/test/");
  }

  SECTION("Archive reader") {
    test(&CreateArchiveDataReader, "data/test.tar.gz");
  }

  SECTION("Memory mapped uncompressed tar archive") {
    test(&CreateMemoryMappedTarDataReader, "data/test.tar");
  }
}

TEST_CASE("test IDataSpan View helper", "[util][data_reader]") {
  using namespace giga_edge;

  auto reader = CreateDirectoryDataReader("data/test/");
  auto span = EnsureNotNull(reader->ReadPersistentData("test1.txt"));

  REQUIRE(span->View() == "Test 1");
  REQUIRE(span->View().data() == span->Get().first);
  REQUIRE(span->View().size() == span->Get().second);
}

TEST_CASE("test IDataReader Exists and IsDirectory", "[util][data_reader]") {
  using namespace giga_edge;

  SECTION("Directory reader") {
    TestExistsAndIsDirectory(CreateDirectoryDataReader("data/test/"));
  }

  SECTION("Archive reader") {
    TestExistsAndIsDirectory(CreateArchiveDataReader("data/test.tar"));
  }

  SECTION("Persistent tar buffer reader") {
    auto tar_data = EnsureNotNull(ReadFileTo<IDataSpanPtr>("data/test.tar"));
    TestExistsAndIsDirectory(CreatePersistentTarBufferDataReader(tar_data));
  }

  SECTION("Temporary tar buffer reader") {
    auto buf = ReadFile("data/test.tar").value();
    TestExistsAndIsDirectory(CreateTemporaryTarBufferDataReader(buf.data(), buf.size()));
  }

  SECTION("Memory mapped tar reader") {
    TestExistsAndIsDirectory(CreateMemoryMappedTarDataReader("data/test.tar"));
  }

  SECTION("Memory mapped directory reader") {
    TestExistsAndIsDirectory(CreateMemoryMappedDirectoryDataReader("data/test/"));
  }
}

TEST_CASE("lazy reader survives Free while another thread reads", "[util][data_reader]") {
  auto reader = giga_edge::CreateLazyCachingDataReader([] {
    return giga_edge::CreateDirectoryDataReader("data/test");
  });
  std::atomic<bool> stop = false;
  std::thread freeing([&reader, &stop] {
    while (!stop) {
      reader->Free();
    }
  });
  for (int i = 0; i < 20000; ++i) {
    REQUIRE(ToString(reader->ReadTransientData("test1.txt")) == "Test 1");
    REQUIRE(reader->Exists("test2.txt"));
  }
  stop = true;
  freeing.join();
}

TEST_CASE("archives of model parts open in parallel", "[util][data_reader]") {
  // Submodels load in parallel, and each one opens the archive of its own part.
  std::atomic<int> read{0};
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&read, i] {
      const auto reader = giga_edge::CreateDataReader(i % 2 == 0 ? "data/test.tar" : "data/test.tar.gz");
      if (ToString(reader->ReadTransientData("folder/test3.txt")) == "Test 3") {
        ++read;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  REQUIRE(read == 8);
}

TEST_CASE("archives list directories while other archives read entries in parallel", "[util][data_reader]") {
  std::ifstream file("data/test.tar", std::ios::binary);
  const std::vector<char> tar{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  const std::vector<std::function<giga_edge::IDataReaderPtr()>> readers{
    [] {
      return giga_edge::CreateArchiveDataReader("data/test.tar");
    },
    [] {
      return giga_edge::CreateMemoryMappedTarDataReader("data/test.tar");
    },
    [&tar] {
      return giga_edge::CreateTemporaryTarBufferDataReader(tar.data(), tar.size());
    },
  };
  constexpr int thread_count = 12;
  constexpr int rounds = 20;
  std::atomic<int> done{0};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i] {
      const auto reader = readers[i % readers.size()]();
      for (int round = 0; round < rounds; ++round) {
        const bool good = i % 2 == 0
          ? reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"}
          : ToString(reader->ReadTransientData("folder/test3.txt")) == "Test 3";
        done += good ? 1 : 0;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  REQUIRE(done == thread_count * rounds);
}
