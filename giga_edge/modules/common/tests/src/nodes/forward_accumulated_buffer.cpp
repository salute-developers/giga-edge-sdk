#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/forward_accumulated_buffer/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Forward accumulated buffer node", "[runtime][nodes]") {
  auto node = CreateNode("forward_accumulated_buffer", "", {}, {"drop"});

  NodePush(node, "input", std::string("test"));
  NodePush(node, "drop", false);
  REQUIRE_THAT(NodeGet<std::string>(node, "output"), Catch::Equals(std::string("test")));

  NodePush(node, "input", std::string(" case"));
  NodePush(node, "drop", false);
  REQUIRE_THAT(NodeGet<std::string>(node, "output"), Catch::Equals(std::string("test case")));

  NodePush(node, "input", std::string("case"));
  NodePush(node, "drop", true);
  REQUIRE_THAT(NodeGet<std::string>(node, "output"), Catch::Equals(std::string("case")));
}
