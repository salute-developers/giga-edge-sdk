#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/synthesis_text/nodes/synthesis_text_buffer/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Synthesis text buffer", "[runtime][nodes]") {
  auto node = CreateNode("synthesis_text_buffer", "");

  {
    INFO("No synthesis text");
    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
  }

  {
    INFO("Set synthesis text");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "text"}});

    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string("text"));
  }

  {
    INFO("Reset synthesis text");
    NodePushEvent(node, Event{.event = ResetSynthesisTextEvent{}});

    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
  }

  {
    INFO("Not allowed output");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "text"}});

    NodePush(node, "allow_output", false);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
  }
}

TEST_CASE("Synthesis text buffer with delay", "[runtime][nodes]") {
  auto node = CreateNode(
    "synthesis_text_buffer",
    R"(
      reset_synthesis_text_delay: 1
    )"
  );

  {
    INFO("No synthesis text");
    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
  }

  {
    INFO("Set and reset synthesis text with delay");
    NodePushEvent(
      node,
      Event{
        .event = SetSynthesisTextEvent{.synthesis_text = "text"},
      }
    );

    NodePushEvent(
      node,
      Event{
        .event = ResetSynthesisTextEvent{},
      }
    );

    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string("text"));

    INFO("Delay is ended -> text should be empty");

    NodePush(node, "allow_output", true);

    REQUIRE(NodeGet<std::string>(node, "synthesis_text") == std::string(""));
  }
}
