#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/clone_input_bool/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Clone input bool node", "[runtime][nodes]") {
  auto node = CreateNode("clone_input_bool", "");

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "first_output") == true);
  REQUIRE(NodeGet<bool>(node, "second_output") == true);
}
