#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/aggregation/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Aggregation node, MAX", "[runtime][nodes]") {
  auto node = CreateNode(
    "aggregation",
    R"(
      aggregation: max
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3}));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Aggregation, MIN", "[runtime][nodes]") {
  auto node = CreateNode(
    "aggregation",
    R"(
      aggregation: min
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1}));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Aggregation, SUM", "[runtime][nodes]") {
  auto node = CreateNode(
    "aggregation",
    R"(
      aggregation: sum
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.6}));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Aggregation, AVG", "[runtime][nodes]") {
  auto node = CreateNode(
    "aggregation",
    R"(
      aggregation: avg
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.25}));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Aggregation, LAST", "[runtime][nodes]") {
  auto node = CreateNode(
    "aggregation",
    R"(
      aggregation: last
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.4}));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

namespace giga_edge {

inline bool operator==(const AggregationNodeParams& lhs, const AggregationNodeParams& rhs) {
  return lhs.aggregation == rhs.aggregation;
}

} // namespace giga_edge

TEST_CASE("Aggregation node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<AggregationNodeParams>(YAML::Load("aggregation: min\n"))
    == AggregationNodeParams{
      .aggregation = AggregationType::MIN,
    }
  );
  REQUIRE(
    ParseConfig<AggregationNodeParams>(YAML::Load("aggregation: max\n"))
    == AggregationNodeParams{
      .aggregation = AggregationType::MAX,
    }
  );
  REQUIRE(
    ParseConfig<AggregationNodeParams>(YAML::Load("aggregation: sum\n"))
    == AggregationNodeParams{
      .aggregation = AggregationType::SUM,
    }
  );
  REQUIRE(
    ParseConfig<AggregationNodeParams>(YAML::Load("aggregation: avg\n"))
    == AggregationNodeParams{
      .aggregation = AggregationType::AVG,
    }
  );
  REQUIRE(
    ParseConfig<AggregationNodeParams>(YAML::Load("aggregation: last\n"))
    == AggregationNodeParams{
      .aggregation = AggregationType::LAST,
    }
  );
}