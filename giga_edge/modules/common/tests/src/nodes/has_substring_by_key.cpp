#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/has_substring_by_key/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Has substring by key node", "[runtime][nodes]") {
  auto node = CreateNode(
    "has_substring_by_key",
    R"(
      substring_files:
        key1: "substrings1.txt"
        key2: "substrings2.txt"
    )",
    {
      {"substrings1.txt", ToDataSpan("один\nдва\nтри")},
      {"substrings2.txt", ToDataSpan("qwe")},
    }
  );

  NodePush(node, "text", std::string{"один"});
  NodePush(node, "key", std::string{"key1"});
  REQUIRE(NodeGet<bool>(node, "has_substring"));

  NodePush(node, "text", std::string{"три"});
  NodePush(node, "key", std::string{"key1"});
  REQUIRE(NodeGet<bool>(node, "has_substring"));

  NodePush(node, "text", std::string{"текст"});
  NodePush(node, "key", std::string{"key1"});
  REQUIRE(!NodeGet<bool>(node, "has_substring"));

  NodePush(node, "text", std::string{"qweasd"});
  NodePush(node, "key", std::string{"key2"});
  REQUIRE(NodeGet<bool>(node, "has_substring"));

  NodePush(node, "text", std::string{"один"});
  NodePush(node, "key", std::string{"key2"});
  REQUIRE(!NodeGet<bool>(node, "has_substring"));

  NodePush(node, "text", std::string{"один два qwe"});
  NodePush(node, "key", std::string{"key3"});
  REQUIRE(!NodeGet<bool>(node, "has_substring"));
}

namespace giga_edge {

inline bool operator==(const HasSubstringByKeyNodeParams& lhs, const HasSubstringByKeyNodeParams& rhs) {
  return lhs.substring_files == rhs.substring_files;
}

} // namespace giga_edge

TEST_CASE("Has substring by key, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<HasSubstringByKeyNodeParams>(YAML::Load(
      "substring_files:\n"
      "  key1: substrings1.txt\n"
      "  key2: substrings2.txt\n"
    ))
    == HasSubstringByKeyNodeParams{
      .substring_files = {
        {"key1", "substrings1.txt"},
        {"key2", "substrings2.txt"},
      }
    }
  );
}