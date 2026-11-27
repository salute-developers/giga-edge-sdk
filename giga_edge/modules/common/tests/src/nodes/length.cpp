#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/length/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Length node, string input", "[runtime][nodes]") {
  auto node = CreateNode("length", "", {}, {"string"});

  NodePush(node, "string", std::string("hello"));
  REQUIRE(NodeGet<float>(node, "length") == 5.0f);

  NodePush(node, "string", std::string("привет"));
  REQUIRE(NodeGet<float>(node, "length") == 6.0f);

  NodePush(node, "string", std::string());
  REQUIRE(NodeGet<float>(node, "length") == 0.0f);
}

TEST_CASE("Length node, float_vector input", "[runtime][nodes]") {
  auto node = CreateNode("length", "", {}, {"float_vector"});

  NodePush(node, "float_vector", std::vector<float>{1.0f, 2.0f, 3.0f});
  REQUIRE(NodeGet<float>(node, "length") == 3.0f);

  NodePush(node, "float_vector", std::vector<float>{});
  REQUIRE(NodeGet<float>(node, "length") == 0.0f);
}

TEST_CASE("Length node, multiple inputs", "[runtime][nodes]") {
  REQUIRE_THROWS_MATCHES(
    CreateNode("length", "", {}, {"string", "float_vector"}),
    std::runtime_error,
    Catch::Message("LengthNode: exactly one of data inputs should be present")
  );
}

TEST_CASE("Length node, no inputs", "[runtime][nodes]") {
  REQUIRE_THROWS_MATCHES(
    CreateNode("length", "", {}, {}),
    std::runtime_error,
    Catch::Message("LengthNode: exactly one of data inputs should be present")
  );
}
