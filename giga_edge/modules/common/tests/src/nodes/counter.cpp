#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/counter/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Counter node", "[runtime][nodes]") {
  auto node = CreateNode(
    "counter",
    R"(
        init_value: 0.0
    )",
    {},
    {"need_reset"}
  );

  NodePush(node, "add", 1.0f);
  NodePush(node, "need_reset", false);
  REQUIRE(NodeGet<float>(node, "output") == 1.0f);

  NodePush(node, "add", 1.0f);
  NodePush(node, "need_reset", false);
  REQUIRE(NodeGet<float>(node, "output") == 2.0f);

  NodePush(node, "add", 1.0f);
  NodePush(node, "need_reset", true);
  REQUIRE(NodeGet<float>(node, "output") == 1.0f);

  NodePush(node, "add", -3.0f);
  NodePush(node, "need_reset", false);
  REQUIRE(NodeGet<float>(node, "output") == -2.0f);
}
