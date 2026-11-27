#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/to_bool/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("To bool node, int input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_bool",
    R"(
      threshold: 3
    )",
    {},
    {"int"}
  );

  NodePush(node, "int", 5);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "int", 1);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("To bool node, float input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_bool",
    R"(
      threshold: 0.5
    )",
    {},
    {"float"}
  );

  NodePush(node, "float", 1.0f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "float", 0.0f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("To bool node, string input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_bool",
    R"(
      threshold: 0.5
    )",
    {},
    {"string"}
  );

  std::string empty_example_str = "";
  NodePush(node, "string", empty_example_str);
  REQUIRE(NodeGet<bool>(node, "output") == false);

  std::string non_empty_example_str = "spottter";
  NodePush(node, "string", non_empty_example_str);
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("To bool node, int_vector input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_bool",
    R"(
      threshold: 0.5
    )",
    {},
    {"int_vector"}
  );

  NodePush(node, "int_vector", std::vector<int>{1});
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "int_vector", std::vector<int>{0});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "int_vector", std::vector<int>{});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  REQUIRE_THROWS_MATCHES(
    NodePush(node, "int_vector", std::vector<int>{1, 2, 3}),
    std::runtime_error,
    Catch::Message("ToBoolNode: Got int_vector input with more than one element")
  );
}

TEST_CASE("To bool node, float_vector input", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_bool",
    R"(
      threshold: 0.5
    )",
    {},
    {"float_vector"}
  );

  NodePush(node, "float_vector", std::vector<float>{1});
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "float_vector", std::vector<float>{0});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "float_vector", std::vector<float>{});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  REQUIRE_THROWS_MATCHES(
    NodePush(node, "float_vector", std::vector<float>{1, 2, 3}),
    std::runtime_error,
    Catch::Message("ToBoolNode: Got float_vector input with more than one element")
  );
}

TEST_CASE("To bool node, threshold as input", "[runtime][nodes]") {
  auto node = CreateNode("to_bool", "", {}, {"float_vector", "threshold"});

  NodePush(node, "float_vector", std::vector<float>{1.0});
  NodePush(node, "threshold", 0.5f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "float_vector", std::vector<float>{1.0});
  NodePush(node, "threshold", 2.0f);
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "float_vector", std::vector<float>{});
  NodePush(node, "threshold", -10.0f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("To bool node, multiple inputs", "[runtime][nodes]") {
  REQUIRE_THROWS_MATCHES(
    CreateNode(
      "to_bool",
      R"(
        threshold: 0.5
      )",
      {},
      {"float_vector", "int"}
    ),
    std::runtime_error,
    Catch::Message("ToBoolNode: exactly one of data inputs should be present")
  );
}

namespace giga_edge {

inline bool operator==(const ToBoolNodeParams& lhs, const ToBoolNodeParams& rhs) {
  return lhs.threshold == rhs.threshold;
}

} // namespace giga_edge

TEST_CASE("To bool node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<ToBoolNodeParams>(YAML::Load("threshold: 0.5\n"))
    == ToBoolNodeParams{
      .threshold = 0.5,
    }
  );
  REQUIRE(
    ParseConfig<ToBoolNodeParams>(YAML::Load(""))
    == ToBoolNodeParams{
      .threshold = 0.0,
    }
  );
}