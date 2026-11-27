#include "temp_dir.h"

#include "giga_edge/lib/runtime_manager/runtime_manager.h"

#include <catch.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace giga_edge;
using tests::TempModelDir;
using tests::WriteConfig;

namespace {

// Minimal ustar writer: just enough structure for the in-memory reader tests.
std::vector<char> MakeTarBuffer(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::vector<char> buffer;
  for (const auto& [name, content] : entries) {
    if (name.size() >= 100) {
      throw std::runtime_error("ustar name field fits 100 bytes, entry name is longer: " + name);
    }
    std::vector<char> header(512, '\0');
    const auto put_octal = [&header](size_t offset, size_t width, uint64_t value) {
      std::snprintf(
        header.data() + offset, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(value)
      );
    };

    std::snprintf(header.data(), 100, "%s", name.c_str());
    put_octal(100, 8, 0644); // mode
    put_octal(108, 8, 0);    // uid
    put_octal(116, 8, 0);    // gid
    put_octal(124, 12, content.size());
    put_octal(136, 12, 0); // mtime
    std::fill(header.begin() + 148, header.begin() + 156, ' ');
    header[156] = '0'; // regular file
    std::memcpy(header.data() + 257, "ustar\00000", 8);

    unsigned checksum = 0;
    for (const char byte : header) {
      checksum += static_cast<unsigned char>(byte);
    }
    std::snprintf(header.data() + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';

    buffer.insert(buffer.end(), header.begin(), header.end());
    buffer.insert(buffer.end(), content.begin(), content.end());
    buffer.resize((buffer.size() + 511) / 512 * 512, '\0');
  }

  buffer.resize(buffer.size() + 1024, '\0');
  return buffer;
}

class TarBufferHolder : public ITemporaryDataHolder {
public:
  explicit TarBufferHolder(std::vector<char> data)
    : data_(std::move(data)) {}

  std::pair<const char*, size_t> Data() const override {
    return {data_.data(), data_.size()};
  }

private:
  std::vector<char> data_;
};

constexpr const char* kCoreConfig = R"(num_threads: 1
version: test
lib_version: 1.18

nodes:
  - type: logical_expression
    name: pass_through
    inputs:
      input: input
    outputs:
      result: output
    params:
      expression: 'input'
)";

RuntimePart MakeCorePart(const TempModelDir& tmp) {
  WriteConfig(tmp.path / "core", kCoreConfig);
  return RuntimePart{
    .id = "core",
    .data = ModelFileData{(tmp.path / "core").string()},
    .versions = {{"core", {.config_path = "config.yaml"}}},
  };
}

RuntimePart MakeTarPart(std::vector<char> tar) {
  return RuntimePart{
    .id = "tar",
    .data = ModelTemporaryTarData{
      .data_provider = [tar = std::move(tar)]() -> ITemporaryDataHolderPtr {
        return std::make_unique<TarBufferHolder>(tar);
      },
    },
    .versions = {{"tar_v", {.config_path = "config.yaml"}}},
  };
}

} // namespace

TEST_CASE("Temporary tar part exposes the full reader interface", "[runtime][runtime_manager]") {
  TempModelDir tmp;
  const auto tar = MakeTarBuffer({{"config.yaml", "version: test\n"}, {"folder/file.txt", "data"}});
  RuntimeManager manager(MakeCorePart(tmp), {MakeTarPart(tar)}, {}, {});

  const auto reader = manager.GetDataReader("tar");
  REQUIRE(reader->ListDirectories("") == std::vector<std::filesystem::path>{"folder"});
  REQUIRE(reader->ListFiles("folder") == std::vector<std::filesystem::path>{"folder/file.txt"});
  const auto span = reader->ReadTransientData("folder/file.txt");
  REQUIRE(span->View() == "data");
}

TEST_CASE("Part configs are parsed once across state updates", "[runtime][runtime_manager]") {
  constexpr const char* k_feature_core_config = R"(num_threads: 1
version: test
lib_version: 1.18

submodels:
  - id: tar_sub
    inputs:
      signal: input
    outputs:
      out: output

features:
  - feature_ids: [tar_feature]
    submodel_id: tar_sub
    submodel_version: tar_v
)";

  constexpr const char* k_tar_submodel_config = R"(nodes:
  - type: logical_op
    name: stub_producer
    inputs:
      first_input: signal
    outputs:
      output: out
    params:
      logical_op_type: not

features:
  - feature_ids: [tar_feature]
    submodel_id: tar_sub
    submodel_version: tar_v
)";

  TempModelDir tmp;
  WriteConfig(tmp.path / "core", k_feature_core_config);
  const auto tar = MakeTarBuffer({{"config.yaml", k_tar_submodel_config}});

  int provider_calls = 0;
  RuntimePart tar_part{
    .id = "tar",
    .data = ModelTemporaryTarData{
      .data_provider = [&tar, &provider_calls]() -> ITemporaryDataHolderPtr {
        ++provider_calls;
        return std::make_unique<TarBufferHolder>(tar);
      },
    },
    .versions = {{"tar_v", {.config_path = "config.yaml"}}},
  };

  RuntimeManager manager(
    RuntimePart{
      .id = "core",
      .data = ModelFileData{(tmp.path / "core").string()},
      .versions = {{"core", {.config_path = "config.yaml"}}},
    },
    {std::move(tar_part)},
    {},
    {"tar_feature"}
  );
  REQUIRE(provider_calls == 1);

  const auto action = manager.InitialUpdateState();
  REQUIRE(provider_calls == 1);
  REQUIRE(action.available_features == AvailableFeatures{"tar_feature"});
}
