#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/concat_vec_float/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Concat vec_float", "[runtime][nodes]") {
  auto node = CreateNode("concat_vec_float", "", {}, {}, {"input_1", "input_2", "input_3"});

  NodePush(node, "input_1", std::vector<float>{1, 2, 3});
  NodePush(node, "input_2", std::vector<float>{4, 5, 6});
  NodePush(node, "input_3", std::vector<float>{7, 8, 9});
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9});
}