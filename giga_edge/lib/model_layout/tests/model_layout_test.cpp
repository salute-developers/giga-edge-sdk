#include "giga_edge/lib/model_layout/model_layout.h"

#include "giga_edge/lib/data_reader/file_path.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <catch.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <variant>

namespace fs = std::filesystem;
using giga_edge::ModelFileData;
using giga_edge::ReadModelLayout;
using giga_edge::ReadPart;

namespace {

std::atomic<int> g_temp_dir_counter{0};

struct TempDir {
  fs::path path;
  TempDir()
    // Unique across parallel test processes (ctest -j).
    : path(
        fs::temp_directory_path()
        / ("model_layout_" + std::to_string(std::random_device{}()) + "_" + std::to_string(g_temp_dir_counter.fetch_add(1)))
      ) {
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    fs::remove_all(path);
  }
};

void WriteFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << content;
}

const std::string& DataPath(const giga_edge::RuntimePart& part) {
  return std::get<ModelFileData>(part.data).path;
}

} // namespace

TEST_CASE("part in the Hugging Face layout keeps configs under model/", "[model_layout]") {
  TempDir dir;
  WriteFile(dir.path / "llm" / "meta.json", R"({"versions": {"llm.a": "config_a.yaml", "llm.b": "config_b.yaml"}})");
  WriteFile(dir.path / "llm" / "model" / "config_a.yaml", "version: llm.a");
  const auto part = ReadPart(dir.path / "llm", "llm");
  REQUIRE(part.id == "llm");
  REQUIRE(DataPath(part) == (dir.path / "llm" / "model").string());
  REQUIRE(part.versions.size() == 2);
  REQUIRE(part.versions.at("llm.a").config_path == "config_a.yaml");
}

TEST_CASE("part may be delivered as an archive", "[model_layout]") {
  TempDir dir;
  WriteFile(dir.path / "llm" / "meta.json", R"({"versions": {"llm.a": "config.yaml"}})");
  WriteFile(dir.path / "llm" / "model.tar", "not a real archive, only the path matters here");
  REQUIRE(DataPath(ReadPart(dir.path / "llm", "llm")) == (dir.path / "llm" / "model.tar").string());
}

TEST_CASE("a part without data points at the archive it will be delivered as", "[model_layout]") {
  TempDir dir;
  WriteFile(dir.path / "llm" / "meta.json", R"({"versions": {"llm.a": "config.yaml"}})");
  REQUIRE(DataPath(ReadPart(dir.path / "llm", "llm")) == (dir.path / "llm" / "model.tar").string());
}

TEST_CASE("a part needs meta.json", "[model_layout]") {
  TempDir dir;
  fs::create_directories(dir.path / "eou" / "model");
  REQUIRE_THROWS(ReadPart(dir.path / "eou", "eou"));
  WriteFile(dir.path / "eou" / "meta.json", R"({"versions": {}})");
  REQUIRE_THROWS(ReadPart(dir.path / "eou", "eou"));
}

TEST_CASE("model meta.json assembles parts from relative paths", "[model_layout]") {
  TempDir dir;
  WriteFile(dir.path / "llm" / "meta.json", R"({"versions": {"llm.a": "config_a.yaml"}})");
  WriteFile(dir.path / "llm" / "model" / "config_a.yaml", "version: llm.a");
  WriteFile(dir.path / "app" / "meta.json", R"({"parts": {"llm": "../llm", "asr": "../asr"}})");
  WriteFile(dir.path / "asr" / "meta.json", R"({"versions": {"asr.a": "config.yaml"}})");
  fs::create_directories(dir.path / "app" / "core");
  const auto layout = ReadModelLayout(dir.path / "app");
  REQUIRE(layout.core.id == "core");
  REQUIRE(DataPath(layout.core) == (dir.path / "app" / "core").string());
  REQUIRE(layout.parts.size() == 2);
  REQUIRE(layout.parts[0].id == "asr");
  REQUIRE(DataPath(layout.parts[0]) == (dir.path / "app" / "../asr" / "model.tar").string());
  REQUIRE(layout.parts[1].id == "llm");
  REQUIRE(DataPath(layout.parts[1]) == (dir.path / "app" / "../llm" / "model").string());
}

TEST_CASE("an optional part may be absent until it is delivered", "[model_layout]") {
  TempDir dir;
  WriteFile(dir.path / "asr" / "meta.json", R"({"versions": {"asr.cpu": "config.yaml"}})");
  WriteFile(
    dir.path / "meta.json", R"({"parts": {"asr": "asr", "asr_ane": "asr_ane"}, "optional_parts": ["asr_ane"]})"
  );
  fs::create_directories(dir.path / "core");
  auto layout = ReadModelLayout(dir.path);
  REQUIRE(layout.parts.size() == 1);
  REQUIRE(layout.parts[0].id == "asr");

  WriteFile(dir.path / "asr_ane" / "meta.json", R"({"versions": {"asr.ane": "config.yaml"}})");
  layout = ReadModelLayout(dir.path);
  REQUIRE(layout.parts.size() == 2);
  REQUIRE(layout.parts[1].id == "asr_ane");

  fs::remove_all(dir.path / "asr");
  REQUIRE_THROWS_WITH(ReadModelLayout(dir.path), Catch::Contains("no part directory"));
  WriteFile(dir.path / "meta.json", R"({"parts": {"asr_ane": "asr_ane"}, "optional_parts": ["asr"]})");
  REQUIRE_THROWS_WITH(ReadModelLayout(dir.path), Catch::Contains("optional part"));
  WriteFile(dir.path / "meta.json", R"({"parts": {"asr_ane": "asr_ane"}, "optional_parts": "asr_ane"})");
  REQUIRE_THROWS_WITH(ReadModelLayout(dir.path), Catch::Contains("not an array"));
}

namespace {

std::string JsonEscaped(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char symbol : text) {
    if (symbol == '\\' || symbol == '"') {
      escaped += '\\';
    }
    escaped += symbol;
  }
  return escaped;
}

} // namespace

TEST_CASE("model paths with non-ASCII names round-trip as UTF-8", "[model_layout]") {
  TempDir dir;
  const auto root = dir.path / giga_edge::FilePath("модель");
  const auto bundled = dir.path / giga_edge::FilePath("Иван Петров") / "resources";
  const auto part = root / giga_edge::FilePath("часть");
  WriteFile(
    root / "meta.json",
    R"({"core": "ядро", "parts": {"llm": "часть", "asr": ")" + JsonEscaped(giga_edge::Utf8Path(bundled / "asr"))
      + R"("}})"
  );
  WriteFile(part / "meta.json", R"({"versions": {"llm.a": "config.yaml"}})");
  WriteFile(part / "model" / "config.yaml", "version: llm.a");
  WriteFile(bundled / "asr" / "meta.json", R"({"versions": {"asr.a": "config.yaml"}})");
  WriteFile(bundled / "asr" / "model" / "config.yaml", "version: asr.a");
  fs::create_directories(root / giga_edge::FilePath("ядро"));
  const auto layout = ReadModelLayout(giga_edge::FilePath(giga_edge::Utf8Path(root)));
  REQUIRE(DataPath(layout.core) == giga_edge::Utf8Path(root / giga_edge::FilePath("ядро")));
  REQUIRE(giga_edge::IsDirectory(DataPath(layout.core)));
  REQUIRE(layout.parts.size() == 2);
  REQUIRE(layout.parts[0].id == "asr");
  REQUIRE(DataPath(layout.parts[0]) == giga_edge::Utf8Path(bundled / "asr" / "model"));
  REQUIRE(layout.parts[1].id == "llm");
  REQUIRE(DataPath(layout.parts[1]) == giga_edge::Utf8Path(part / "model"));
  for (const auto& entry : layout.parts) {
    REQUIRE(giga_edge::IsDirectory(DataPath(entry)));
    REQUIRE(giga_edge::ReadFile(DataPath(entry) + "/config.yaml").has_value());
  }
}

TEST_CASE("model without meta.json is an error", "[model_layout]") {
  TempDir dir;
  fs::create_directories(dir.path / "core");
  REQUIRE_THROWS(ReadModelLayout(dir.path));
}

TEST_CASE("missing model directory is an error", "[model_layout]") {
  REQUIRE_THROWS(ReadModelLayout("/nonexistent/app/model"));
}
