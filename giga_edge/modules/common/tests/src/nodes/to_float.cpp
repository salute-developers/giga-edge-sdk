#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/to_float/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("To float node, int input", "[runtime][nodes]") {
  auto node = CreateNode("to_float", "", {}, {"int"});

  NodePush(node, "int", 5);
  REQUIRE(NodeGet<float>(node, "output") == 5.0);
}

TEST_CASE("To float node, int_vector input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_float",
    R"(
      on_empty: 0.5
    )",
    {},
    {"int_vector"}
  );

  NodePush(node, "int_vector", std::vector<int>{1});
  REQUIRE(NodeGet<float>(node, "output") == 1.0);

  NodePush(node, "int_vector", std::vector<int>{0});
  REQUIRE(NodeGet<float>(node, "output") == 0.0);

  NodePush(node, "int_vector", std::vector<int>{});
  REQUIRE(NodeGet<float>(node, "output") == 0.5);

  REQUIRE_THROWS_MATCHES(
    NodePush(node, "int_vector", std::vector<int>{1, 2, 3}),
    std::runtime_error,
    Catch::Message("ToFloatNode: Got int_vector input with more than one element")
  );
}

TEST_CASE("To float node, float_vector input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_float",
    R"(
      on_empty: 0.5
    )",
    {},
    {"float_vector"}
  );

  NodePush(node, "float_vector", std::vector<float>{1});
  REQUIRE(NodeGet<float>(node, "output") == 1.0);

  NodePush(node, "float_vector", std::vector<float>{0});
  REQUIRE(NodeGet<float>(node, "output") == 0.0);

  NodePush(node, "float_vector", std::vector<float>{});
  REQUIRE(NodeGet<float>(node, "output") == 0.5);

  REQUIRE_THROWS_MATCHES(
    NodePush(node, "float_vector", std::vector<float>{1, 2, 3}),
    std::runtime_error,
    Catch::Message("ToFloatNode: Got float_vector input with more than one element")
  );
}

TEST_CASE("To float node, multiple inputs", "[runtime][nodes]") {
  REQUIRE_THROWS_MATCHES(
    CreateNode("to_float", "", {}, {"float_vector", "int"}),
    std::runtime_error,
    Catch::Message("ToFloatNode: exactly one of data inputs should be present")
  );
}

namespace giga_edge {

inline bool operator==(const ToFloatNodeParams& lhs, const ToFloatNodeParams& rhs) {
  return lhs.on_empty == rhs.on_empty;
}

} // namespace giga_edge

TEST_CASE("To float node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<ToFloatNodeParams>(YAML::Load("on_empty: 0.5\n"))
    == ToFloatNodeParams{
      .on_empty = 0.5,
    }
  );
  REQUIRE(
    ParseConfig<ToFloatNodeParams>(YAML::Load(""))
    == ToFloatNodeParams{
      .on_empty = 0.0,
    }
  );
}