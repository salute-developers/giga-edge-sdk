#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/data_gate/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Data gate node", "[runtime][nodes]") {
  auto node = CreateNode("data_gate", "", {}, {});

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "gate", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.5, 0.6});
  NodePush(node, "gate", true);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5, 0.6}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "gate", true);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3}));
}
