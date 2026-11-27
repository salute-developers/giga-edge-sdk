#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/batch_gather_string/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Batch gather string node", "[runtime][nodes][new_1]") {
  auto node = CreateNode("batch_gather_string", "", {}, {});

  NodePush(node, "input", std::string{"he"}, false);
  NodePush(node, "input", std::string{"llo"}, true);

  REQUIRE(NodeGet<std::string>(node, "output") == "hello");

  NodePush(node, "input", std::string{"spo"}, false);
  NodePush(node, "input", std::string{"tter"}, true);

  REQUIRE(NodeGet<std::string>(node, "output") == "spotter");
}
