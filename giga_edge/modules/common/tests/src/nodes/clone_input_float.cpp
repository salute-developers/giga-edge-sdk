#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/clone_input_float/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Clone input float node", "[runtime][nodes]") {
  auto node = CreateNode("clone_input_float", "");
  std::vector<float> example = {1, 2, 3, 4};
  NodePush(node, "input", example);
  REQUIRE(NodeGet<std::vector<float>>(node, "first_output") == example);
  REQUIRE(NodeGet<std::vector<float>>(node, "second_output") == example);
}
