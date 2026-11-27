#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/to_vec_float/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("To vec float node, float input without empty_value", "[runtime][nodes]") {
  auto node = CreateNode("to_vec_float", "", {}, {"float"});

  NodePush(node, "float", 0.5f);
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{0.5f});

  NodePush(node, "float", 0.0f);
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{0.0f});
}

TEST_CASE("To vec float node, float input with empty_value", "[runtime][nodes]") {
  auto node = CreateNode(
    "to_vec_float",
    R"(
      empty_value: 0.0
    )",
    {},
    {"float"}
  );

  NodePush(node, "float", 394.0f);
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{394.0f});

  NodePush(node, "float", 0.0f);
  REQUIRE(NodeGet<std::vector<float>>(node, "output").empty());
}

TEST_CASE("To vec float node, int and bool inputs", "[runtime][nodes]") {
  auto int_node = CreateNode("to_vec_float", "", {}, {"int"});
  NodePush(int_node, "int", 3);
  REQUIRE(NodeGet<std::vector<float>>(int_node, "output") == std::vector<float>{3.0f});

  auto bool_node = CreateNode("to_vec_float", "", {}, {"bool"});
  NodePush(bool_node, "bool", true);
  REQUIRE(NodeGet<std::vector<float>>(bool_node, "output") == std::vector<float>{1.0f});
}

TEST_CASE("To vec float node, vector inputs", "[runtime][nodes]") {
  auto node = CreateNode("to_vec_float", "", {}, {"int_vector"});
  NodePush(node, "int_vector", std::vector<int>{1, 2, 3});
  REQUIRE(NodeGet<std::vector<float>>(node, "output") == std::vector<float>{1.0f, 2.0f, 3.0f});

  auto bools = CreateNode("to_vec_float", "", {}, {"bool_vector"});
  NodePush(bools, "bool_vector", std::vector<bool>{true, false});
  REQUIRE(NodeGet<std::vector<float>>(bools, "output") == std::vector<float>{1.0f, 0.0f});
}

TEST_CASE("To vec float node, exactly one input", "[runtime][nodes]") {
  REQUIRE_THROWS(CreateNode("to_vec_float", "", {}, {"float", "int"}));
}
