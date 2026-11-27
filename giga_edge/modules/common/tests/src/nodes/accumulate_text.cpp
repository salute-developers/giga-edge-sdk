#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/accumulate_text/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Accumulate buffer text node", "[runtime][nodes]") {
  auto node = CreateNode("accumulate_text", "", {}, {"push"});

  NodePush(node, "input", std::string{"he"});
  NodePush(node, "push", false);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"ll"});
  NodePush(node, "push", false);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"o"});
  NodePush(node, "push", true);
  REQUIRE(NodeGet<std::string>(node, "output") == "hello");

  NodePush(node, "input", std::string{"hi"});
  NodePush(node, "push", true);
  REQUIRE(NodeGet<std::string>(node, "output") == "hi");
}

TEST_CASE("Accumulate buffer text node, with drop input", "[runtime][nodes]") {
  auto node = CreateNode("accumulate_text", "", {}, {"push", "drop"});

  NodePush(node, "input", std::string{"hel"});
  NodePush(node, "push", false);
  NodePush(node, "drop", false);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"lo"});
  NodePush(node, "push", false);
  NodePush(node, "drop", true);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"hi"});
  NodePush(node, "push", true);
  NodePush(node, "drop", false);
  REQUIRE(NodeGet<std::string>(node, "output") == "hi");
}

TEST_CASE("Accumulate text node keeps the text after push", "[runtime][nodes]") {
  auto node = CreateNode("accumulate_text", "clear_after_push: false", {}, {"push"});

  NodePush(node, "input", std::string{"he"});
  NodePush(node, "push", true);
  REQUIRE(NodeGet<std::string>(node, "output") == "he");

  NodePush(node, "input", std::string{"llo"});
  NodePush(node, "push", true);
  REQUIRE(NodeGet<std::string>(node, "output") == "hello");
}

TEST_CASE("Accumulate text node restart keeps the tick input", "[runtime][nodes]") {
  auto node = CreateNode("accumulate_text", "", {}, {"push", "restart"});

  NodePush(node, "input", std::string{"old"});
  NodePush(node, "push", false);
  NodePush(node, "restart", false);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"he"});
  NodePush(node, "push", false);
  NodePush(node, "restart", true);
  REQUIRE(NodeGet<std::string>(node, "output").empty());

  NodePush(node, "input", std::string{"llo"});
  NodePush(node, "push", true);
  NodePush(node, "restart", false);
  REQUIRE(NodeGet<std::string>(node, "output") == "hello");
}
