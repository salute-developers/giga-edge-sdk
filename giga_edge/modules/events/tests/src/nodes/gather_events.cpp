#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/nodes/gather_events/node.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Gather events node, activations", "[runtime][nodes]") {
  auto node = CreateNode(
    "gather_events",
    R"(
      event_types:
        - activation
    )",
    {},
    {},
    {"events_1", "events_2", "events_3"}
  );

  NodePush(
    node,
    "events_1",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "model_1"},
      },
      {{.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_1"}},
    }
  );
  NodePush(
    node,
    "events_2",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_2"},
    }}
  );
  NodePush(
    node,
    "events_3",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "model_3"}},
      {{.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_3"}},
    }
  );

  REQUIRE_THAT(
    NodeGet<SpotterEvent>(node, "event"),
    Catch::Matchers::UnorderedEquals(
      SpotterEvent{
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION, .model_name = "model_1"},
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION, .model_name = "model_2"},
      }
    )
  );
}

TEST_CASE("Gather events node, multiple event types", "[runtime][nodes]") {
  auto node = CreateNode(
    "gather_events",
    R"(
      event_types:
        - activation
        - quick_command
    )",
    {},
    {},
    {"events_1", "events_2", "events_3"}
  );

  NodePush(
    node,
    "events_1",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "model_1"},
      },
      {{.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_1"}},
    }
  );
  NodePush(
    node,
    "events_2",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_2"},
    }}
  );
  NodePush(
    node,
    "events_3",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "model_3"}},
      {{.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_3"}},
    }
  );

  REQUIRE_THAT(
    NodeGet<SpotterEvent>(node, "event"),
    Catch::Matchers::UnorderedEquals(
      SpotterEvent{
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_1"},
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "model_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .model_name = "model_3"}
      }
    )
  );
}

namespace giga_edge {

inline bool operator==(const GatherEventsNodeParams& lhs, const GatherEventsNodeParams& rhs) {
  return lhs.event_types == rhs.event_types;
}

} // namespace giga_edge

TEST_CASE("Gather events node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<GatherEventsNodeParams>(YAML::Load("event_types: [activation, subactivation, quick_command]\n"))
    == GatherEventsNodeParams{
      .event_types = {SpotterEventType::ACTIVATION, SpotterEventType::SUBACTIVATION, SpotterEventType::QUICK_COMMAND},
    }
  );
  REQUIRE(ParseConfig<GatherEventsNodeParams>(YAML::Load("")) == GatherEventsNodeParams{});
}