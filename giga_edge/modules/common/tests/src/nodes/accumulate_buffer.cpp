#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/accumulate_buffer/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Accumulate buffer node", "[runtime][nodes]") {
  auto node = CreateNode(
    "accumulate_buffer",
    "",
    {},
    {
      "push",
    }
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "push", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.4, 0.5});
  NodePush(node, "push", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.6});
  NodePush(node, "push", true);
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3, 0.4, 0.5, 0.6})
  );

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "push", true);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3}));
}

TEST_CASE("Accumulate buffer node, with drop input", "[runtime][nodes]") {
  auto node = CreateNode("accumulate_buffer", "", {}, {"push", "drop"});

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "push", false);
  NodePush(node, "drop", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.4, 0.5});
  NodePush(node, "push", false);
  NodePush(node, "drop", true);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.6});
  NodePush(node, "push", true);
  NodePush(node, "drop", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.6}));

  NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
  NodePush(node, "push", false);
  NodePush(node, "drop", false);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "input", std::vector<float>{0.4, 0.5});
  NodePush(node, "push", true);
  NodePush(node, "drop", true);
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

TEST_CASE("Accumulate buffer node, operations_priority", "[runtime][nodes]") {
  SECTION("drop, push, inputs") {
    auto node = CreateNode(
      "accumulate_buffer",
      R"(
        operations_priority: [drop, push, inputs]
      )",
      {},
      {"push", "drop"}
    );

    NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
    NodePush(node, "push", false);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{0.4, 0.5});
    NodePush(node, "push", true);
    NodePush(node, "drop", true);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{0.6, 0.7});
    NodePush(node, "push", true);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.4, 0.5}));
  }

  SECTION("inputs, drop, push") {
    auto node = CreateNode(
      "accumulate_buffer",
      R"(
        operations_priority: [inputs, drop, push]
      )",
      {},
      {"push", "drop"}
    );

    NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
    NodePush(node, "push", false);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{0.4, 0.5});
    NodePush(node, "push", true);
    NodePush(node, "drop", true);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{0.6, 0.7});
    NodePush(node, "push", true);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.6, 0.7}));
  }

  SECTION("push, inputs, drop") {
    auto node = CreateNode(
      "accumulate_buffer",
      R"(
        operations_priority: [push, inputs, drop]
      )",
      {},
      {"push", "drop"}
    );

    NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
    NodePush(node, "push", false);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{0.4, 0.5});
    NodePush(node, "push", true);
    NodePush(node, "drop", true);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.1, 0.2, 0.3}));

    NodePush(node, "input", std::vector<float>{0.6, 0.7});
    NodePush(node, "push", true);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

    NodePush(node, "input", std::vector<float>{});
    NodePush(node, "push", true);
    NodePush(node, "drop", false);
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.6, 0.7}));
  }
}

TEST_CASE("Accumulate buffer node, max_size", "[runtime][nodes]") {
  SECTION("clear_after_push: false") {
    auto node = CreateNode(
      "accumulate_buffer",
      R"(
        clear_after_push: false
        max_size: 2
      )"
    );

    NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.2, 0.3}));

    NodePush(node, "input", std::vector<float>{0.4});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.3, 0.4}));

    NodePush(node, "input", std::vector<float>{0.5, 0.6, 0.7, 0.8});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.7, 0.8}));
  }

  SECTION("clear_after_push: true") {
    auto node = CreateNode(
      "accumulate_buffer",
      R"(
        clear_after_push: true
        max_size: 2
      )"
    );

    NodePush(node, "input", std::vector<float>{0.1, 0.2, 0.3});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.2, 0.3}));

    NodePush(node, "input", std::vector<float>{0.4});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.4}));

    NodePush(node, "input", std::vector<float>{0.5, 0.6, 0.7, 0.8});
    REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{0.7, 0.8}));
  }
}