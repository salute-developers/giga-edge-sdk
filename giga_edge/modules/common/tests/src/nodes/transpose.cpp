#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/transpose/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Transpose node", "[runtime][nodes]") {
  auto node = CreateNode(
    "transpose",
    R"(
      columns: 3
    )"
  );

  NodePush(node, "input", std::vector<float>{1, 2, 3, 4, 5, 6});
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{1, 4, 2, 5, 3, 6});

  NodePush(node, "input", std::vector<float>{});
  REQUIRE(NodeGet<std::vector<float>>(node, "output").empty());

  REQUIRE_THROWS(NodePush(node, "input", std::vector<float>{1, 2}));
}

TEST_CASE("Transpose node, columns must be positive", "[runtime][nodes]") {
  REQUIRE_THROWS(CreateNode("transpose", "columns: 0"));
}
