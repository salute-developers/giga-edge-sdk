#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/thresholds_compare/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Threshold compare node", "[runtime][nodes]") {
  auto node = CreateNode(
    "thresholds_compare",
    R"(
        thresholds: [0.1, 0.2, 0.3, 0.4, 0.5]
    )"
  );

  NodePush(node, "input", std::vector<float>{0.7, 0.1, 0.5, 0.9, 0.3});
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1., 0., 1., 1., 0.}).epsilon(1.e-5)
  );
}
