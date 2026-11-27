#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/math_expression/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Math expression node", "[runtime][nodes]") {
  auto node = CreateNode(
    "math_expression",
    R"(
      expression: "a + b / 32 - (x + 1) / y"
    )",
    {},
    {},
    {"a", "b", "x", "y"}
  );

  NodePush(node, "a", 1.0f);
  NodePush(node, "b", 64.0f);
  NodePush(node, "x", 3.0f);
  NodePush(node, "y", 2.0f);
  REQUIRE(NodeGet<float>(node, "result") == 1.0f);

  NodePush(node, "a", 0.0f);
  NodePush(node, "b", 32.0f);
  NodePush(node, "x", 1.0f);
  NodePush(node, "y", 1.0f);
  REQUIRE(NodeGet<float>(node, "result") == -1.0f);
}
