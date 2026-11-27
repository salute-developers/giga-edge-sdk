#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/coalesce_text/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Coalesce text node", "[runtime][nodes]") {
  auto node = CreateNode("coalesce_text", "", {}, {"first"});

  NodePush(node, "first", std::string{"typed"});
  NodePush(node, "second", std::string{"recognized"});
  REQUIRE(NodeGet<std::string>(node, "text") == "typed");

  NodePush(node, "first", std::string{});
  NodePush(node, "second", std::string{"recognized"});
  REQUIRE(NodeGet<std::string>(node, "text") == "recognized");

  NodePush(node, "first", std::string{});
  NodePush(node, "second", std::string{});
  REQUIRE(NodeGet<std::string>(node, "text").empty());
}

TEST_CASE("Coalesce text works without the optional override", "[runtime][nodes]") {
  auto node = CreateNode("coalesce_text", "");
  NodePush(node, "second", std::string{"recognized"});
  REQUIRE(NodeGet<std::string>(node, "text") == "recognized");
}
