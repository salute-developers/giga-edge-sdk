#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/synthesis_text/nodes/synthesis_text/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Synthesis text node", "[runtime][nodes]") {
  auto node = CreateNode("synthesis_text", "", {}, {}, {"some_input"});

  {
    INFO("No synthesis text");
    NodePush(node, "some_input", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
    REQUIRE(NodeGet<bool>(node, "reset_synthesis_text") == false);
  }

  {
    INFO("Set synthesis text");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "text"}});

    NodePush(node, "some_input", 7);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string("text"));
    REQUIRE(NodeGet<bool>(node, "reset_synthesis_text") == false);
  }

  {
    INFO("Reset synthesis text");
    NodePushEvent(node, Event{.event = ResetSynthesisTextEvent{}});

    NodePush(node, "some_input", std::string{"qwe"});

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
    REQUIRE(NodeGet<bool>(node, "reset_synthesis_text") == true);
  }
}
