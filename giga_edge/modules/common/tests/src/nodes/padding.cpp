#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/padding/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Padding node, add to end", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 3
      value: 0.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.0, 0.0, 0.0}));

  NodePush(node, "input", std::vector<float>{0.5});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5, 0.0, 0.0, 0.0}));
}

TEST_CASE("Padding node, add to beginning", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 2
      value: -1.0
      append_place: begin
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{-1.0, -1.0, 0.1, 0.2}));

  NodePush(node, "input", std::vector<float>{0.5, 0.6, 0.7});
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{-1.0, -1.0, 0.5, 0.6, 0.7})
  );
}

TEST_CASE("Padding node, pad to size mode", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 5
      value: 0.5
      append_place: end
      padding_strategy: pad_to_size
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.5, 0.5, 0.5}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4, 0.5});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3, 0.4, 0.5}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4, 0.5, 0.6});
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3, 0.4, 0.5, 0.6})
  );
}

TEST_CASE("Padding node, pad to size at beginning", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 4
      value: 1.0
      append_place: begin
      padding_strategy: pad_to_size
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0, 1.0, 0.1, 0.2}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3, 0.4}));
}

TEST_CASE("Padding node, empty input", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 3
      value: 2.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Padding node, zero count", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 0
      value: 1.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2}));
}

namespace giga_edge {

inline bool operator==(const PaddingParams& lhs, const PaddingParams& rhs) {
  return lhs.count == rhs.count && lhs.value == rhs.value && lhs.append_place == rhs.append_place
    && lhs.padding_strategy == rhs.padding_strategy;
}

} // namespace giga_edge

TEST_CASE("Padding node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<PaddingParams>(YAML::Load(
      "count: 5\n"
      "value: 1.5\n"
      "append_place: end\n"
      "padding_strategy: always_add\n"
    ))
    == PaddingParams{
      .count = 5,
      .value = 1.5f,
      .append_place = PaddingAppendPlace::END,
      .padding_strategy = PaddingStrategy::ALWAYS_ADD,
    }
  );

  REQUIRE(
    ParseConfig<PaddingParams>(YAML::Load(
      "count: 3\n"
      "value: -0.5\n"
      "append_place: begin\n"
      "padding_strategy: pad_to_size\n"
    ))
    == PaddingParams{
      .count = 3,
      .value = -0.5f,
      .append_place = PaddingAppendPlace::BEGIN,
      .padding_strategy = PaddingStrategy::PAD_TO_SIZE,
    }
  );
}

TEST_CASE("Padding node, default parameters", "[runtime][nodes]") {
  auto node = CreateNode(
    "padding",
    R"(
      count: 2
      value: -1.0
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  // Default is BEGIN and ALWAYS_ADD.
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{-1.0, -1.0, 0.1, 0.2}));
}
