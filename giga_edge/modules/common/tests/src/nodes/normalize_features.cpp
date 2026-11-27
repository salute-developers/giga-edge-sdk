#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/normalize_features/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("NormalizeFeatures L2 unit vector", "[runtime][nodes]") {
  auto node = CreateNode("normalize_features", "", {}, {}, {"features"});

  NodePush(node, "features", std::vector<float>{3.f, 4.f});
  const auto out = NodeGet<std::vector<float>>(node, "normalized_features");
  REQUIRE(out.size() == 2);
  REQUIRE(out[0] == Approx(0.6f).margin(1e-5f));
  REQUIRE(out[1] == Approx(0.8f).margin(1e-5f));
}

TEST_CASE("NormalizeFeatures empty input", "[runtime][nodes]") {
  auto node = CreateNode("normalize_features", "", {}, {}, {"features"});

  NodePush(node, "features", std::vector<float>{});
  REQUIRE(NodeGet<std::vector<float>>(node, "normalized_features").empty());
}

TEST_CASE("NormalizeFeatures zero norm", "[runtime][nodes]") {
  auto node = CreateNode("normalize_features", "", {}, {}, {"features"});

  NodePush(node, "features", std::vector<float>{0.f, 0.f});
  REQUIRE(NodeGet<std::vector<float>>(node, "normalized_features").empty());
}
