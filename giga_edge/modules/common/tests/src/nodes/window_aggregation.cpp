#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/window_aggregation/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Aggregation node: last", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 1
          aggregation_type: last
          default_value:
            strategy: fill
            value: 0
      output_interval: 1
    )"
  );

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{2.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{2.}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: max, const", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 3
          aggregation_type: max
          default_value:
            strategy: const
            value: 0.5
      output_interval: 1
    )"
  );

  NodePush(node, "input", std::vector<float>{3.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{2.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{3.0}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: min, const", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 3
          aggregation_type: min
          default_value:
            strategy: const
            value: 0.5
      output_interval: 1
    )"
  );

  NodePush(node, "input", std::vector<float>{3.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{2.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: avg, fill", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 3
          aggregation_type: avg
          default_value:
            strategy: fill
            value: 0
      output_interval: 1
    )"
  );

  NodePush(node, "input", std::vector<float>{3.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{2.});
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{5.0 / 3.0}).epsilon(1.e-5)
  );

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{2.0}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: interval 2", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 1
          aggregation_type: last
          default_value:
            strategy: fill
            value: 0
      output_interval: 2
    )"
  );

  NodePush(node, "input", std::vector<float>{3.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{3.0}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{2.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: interval 1", "[runtime][nodes]") {
  auto node = CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 1
          aggregation_type: last
          default_value:
            strategy: fill
            value: 0
      output_interval: 1
    )"
  );

  NodePush(node, "input", std::vector<float>{3.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{3.0}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{3.0}).epsilon(1.e-5));

  NodePush(node, "input", std::vector<float>{1.});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0}).epsilon(1.e-5));
}

TEST_CASE("Aggregation node: rejects a non-positive window", "[runtime][nodes]") {
  for (const auto* window_size : {"", "0", "1,"}) {
    REQUIRE_THROWS(CreateNode(
      "window_aggregation",
      std::string(R"(
      aggregations:
        - window_size: )")
        + window_size + R"(
          aggregation_type: last
      output_interval: 1
    )"
    ));
  }
}

TEST_CASE("Aggregation node: rejects a non-positive output interval", "[runtime][nodes]") {
  REQUIRE_THROWS(CreateNode(
    "window_aggregation",
    R"(
      aggregations:
        - window_size: 1
          aggregation_type: last
      output_interval: 0
    )"
  ));
}
