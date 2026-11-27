#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/logical_expression/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Logical expression", "[runtime][nodes]") {
  auto node = CreateNode(
    "logical_expression",
    R"(
      expression: "!var1 || !!(var2 && (var3 || var1)) "
    )",
    {},
    {},
    {"var1", "var2", "var3"}
  );

  NodePush(node, "var1", true);
  NodePush(node, "var2", true);
  NodePush(node, "var3", false);
  REQUIRE(NodeGet<bool>(node, "result") == true);

  NodePush(node, "var1", true);
  NodePush(node, "var2", false);
  NodePush(node, "var3", true);
  REQUIRE(NodeGet<bool>(node, "result") == false);
}