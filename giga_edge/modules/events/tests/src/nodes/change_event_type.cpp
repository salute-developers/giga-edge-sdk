#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/nodes/change_event_type/node.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Change event type node, from quick_commad to disable", "[runtime][nodes]") {
  auto node = CreateNode(
    "change_event_type",
    R"(
      input_event_type: quick_command
      output_event_type: disabled
    )",
    {},
    {"flag"}
  );

  {
    INFO("Flag is true");
    NodePush(
      node,
      "input_events",
      std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );

    NodePush(node, "flag", true);

    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::DISABLED},
      }}
    );
  }

  {
    INFO("Flag is false");
    NodePush(
      node,
      "input_events",
      std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );

    NodePush(node, "flag", false);

    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );
  }
}

TEST_CASE("Change event type node, from activation to subactivation", "[runtime][nodes]") {
  auto node = CreateNode(
    "change_event_type",
    R"(
      input_event_type: activation
      output_event_type: subactivation
    )",
    {},
    {"flag"}
  );

  {
    INFO("Flag is true");
    NodePush(
      node,
      "input_events",
      std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );

    NodePush(node, "flag", true);

    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );
  }

  {
    INFO("Flag is false");
    NodePush(
      node,
      "input_events",
      std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );

    NodePush(node, "flag", false);

    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{{
        {.event_type = SpotterEventType::ACTIVATION},
        {.event_type = SpotterEventType::SUBACTIVATION},
        {.event_type = SpotterEventType::QUICK_COMMAND},
      }}
    );
  }
}

namespace giga_edge {

inline bool operator==(const ChangeEventTypeNodeParams& lhs, const ChangeEventTypeNodeParams& rhs) {
  return lhs.input_event_type == rhs.input_event_type && lhs.output_event_type == rhs.output_event_type;
}

} // namespace giga_edge

TEST_CASE("Change event type, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<ChangeEventTypeNodeParams>(YAML::Load("input_event_type: quick_command\noutput_event_type: disabled\n"))
    == ChangeEventTypeNodeParams{
      .input_event_type = SpotterEventType::QUICK_COMMAND, .output_event_type = SpotterEventType::DISABLED
    }
  );
}
