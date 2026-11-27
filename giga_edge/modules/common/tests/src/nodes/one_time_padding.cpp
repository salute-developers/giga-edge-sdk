#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/one_time_padding/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("One time padding node, basic functionality", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 2
      value: 0.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.0, 0.0}));

  NodePush(node, "input", std::vector<float>{0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3, 0.4}));

  NodePush(node, "input", std::vector<float>{0.5});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}));
}

TEST_CASE("One time padding node, reset functionality", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 3
      value: 1.0
      append_place: begin
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0, 1.0, 1.0, 0.1}));

  NodePush(node, "input", std::vector<float>{0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.2}));

  node.node->Reset();

  NodePush(node, "input", std::vector<float>{0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0, 1.0, 1.0, 0.3}));

  NodePush(node, "input", std::vector<float>{0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.4}));
}

TEST_CASE("One time padding node, pad to size mode", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 4
      value: 0.5
      append_place: end
      padding_strategy: pad_to_size
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.5, 0.5}));

  NodePush(node, "input", std::vector<float>{0.3, 0.4, 0.5, 0.6});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3, 0.4, 0.5, 0.6}));

  NodePush(node, "input", std::vector<float>{0.7, 0.8, 0.9, 1.0, 1.1});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.7, 0.8, 0.9, 1.0, 1.1}));
}

TEST_CASE("One time padding node, first input already large enough", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 3
      value: 2.0
      append_place: end
      padding_strategy: pad_to_size
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3, 0.4}));

  NodePush(node, "input", std::vector<float>{0.5});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.5}));
}

TEST_CASE("One time padding node, empty input", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 2
      value: 3.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{3.0, 3.0}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2}));
}

TEST_CASE("One time padding node, zero count", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 0
      value: 1.0
      append_place: end
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2}));

  NodePush(node, "input", std::vector<float>{0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3, 0.4}));
}

TEST_CASE("One time padding node, multiple resets", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 1
      value: 9.0
      append_place: begin
      padding_strategy: always_add
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{9.0, 0.1}));

  NodePush(node, "input", std::vector<float>{0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.2}));

  node.node->Reset();
  NodePush(node, "input", std::vector<float>{0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{9.0, 0.3}));

  NodePush(node, "input", std::vector<float>{0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.4}));

  node.node->Reset();
  NodePush(node, "input", std::vector<float>{0.5});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{9.0, 0.5}));
}

TEST_CASE("One time padding node, default parameters", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 2
      value: -1.0
    )"
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{-1.0, -1.0, 0.1, 0.2}));

  NodePush(node, "input", std::vector<float>{0.3, 0.4});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3, 0.4}));
}

TEST_CASE("One time padding node, need_reset input pads the next stream again", "[runtime][nodes]") {
  auto node = CreateNode(
    "one_time_padding",
    R"(
      count: 2
      value: 0.0
      append_place: begin
      padding_strategy: always_add
    )",
    {},
    {"need_reset"}
  );

  NodePush(node, "need_reset", true);
  NodePush(node, "input", std::vector<float>{0.1});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.0, 0.0, 0.1}));

  NodePush(node, "need_reset", false);
  NodePush(node, "input", std::vector<float>{0.2});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.2}));

  NodePush(node, "need_reset", true);
  NodePush(node, "input", std::vector<float>{0.3});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.0, 0.0, 0.3}));
}
