#include "giga_edge/lib/data_reader/file_path.h"

#include "giga_edge/lib/data_reader/exceptions.h"

#include <catch.hpp>

#include <string>

using giga_edge::FilePath;
using giga_edge::Utf8Path;

TEST_CASE("UTF-8 paths survive the round trip through std::filesystem", "[file_path]") {
  for (const std::string& text :
       {std::string{}, std::string{"model.onnx"}, std::string{"модели/ядро.yaml"}, std::string{"日本語/モデル.tar"}}) {
    INFO(text);
    const auto path = FilePath(text);
    REQUIRE(Utf8Path(path) == text);
    REQUIRE(Utf8Path(FilePath(Utf8Path(path))) == text);
  }
  REQUIRE(FilePath("модели/ядро.yaml").filename() == FilePath("ядро.yaml"));
  REQUIRE(Utf8Path((FilePath("модели") / FilePath("ядро.yaml")).filename()) == "ядро.yaml");
#if defined(_WIN32)
  REQUIRE_THROWS_AS(FilePath("\xff\xfe"), giga_edge::ReaderException);
#endif
}
