#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/is_empty/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Is empty node, bool_vector input", "[runtime][nodes]") {
  auto node = CreateNode("is_empty", "", {}, {"bool_vector"});

  NodePush(node, "bool_vector", std::vector<bool>{false});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "bool_vector", std::vector<bool>{true, true});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "bool_vector", std::vector<bool>{});
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Is empty node, float_vector input", "[runtime][nodes]") {
  auto node = CreateNode("is_empty", "", {}, {"float_vector"});

  NodePush(node, "float_vector", std::vector<float>{4.2});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "float_vector", std::vector<float>{0, 0});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "float_vector", std::vector<float>{});
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Is empty node, int_vector input", "[runtime][nodes]") {
  auto node = CreateNode("is_empty", "", {}, {"int_vector"});

  NodePush(node, "int_vector", std::vector<int>{1});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "int_vector", std::vector<int>{0});
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "int_vector", std::vector<int>{});
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Is empty node, string input", "[runtime][nodes]") {
  auto node = CreateNode("is_empty", "", {}, {"string"});

  NodePush(node, "string", std::string("example"));
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "string", std::string());
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Is empty node, multiple inputs", "[runtime][nodes]") {
  REQUIRE_THROWS_MATCHES(
    CreateNode("is_empty", "", {}, {"bool_vector", "float_vector"}),
    std::runtime_error,
    Catch::Message("IsEmptyNode: exactly one of data inputs should be present")
  );
}