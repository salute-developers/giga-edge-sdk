#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/nodes/filter_events/node.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Filter events node, single event type", "[runtime][nodes]") {
  auto node = CreateNode(
    "filter_events",
    R"(
      event_types:
        - activation
    )"
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );

  REQUIRE(
    NodeGet<SpotterEvent>(node, "event")
    == SpotterEvent{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );
  REQUIRE(
    NodeGet<SpotterEvent>(node, "event")
    == SpotterEvent{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "2"},
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );
  REQUIRE(NodeGet<SpotterEvent>(node, "event").empty());
}

TEST_CASE("Filter events node, multiple event types", "[runtime][nodes]") {
  auto node = CreateNode(
    "filter_events",
    R"(
      event_types:
        - activation
        - quick_command
    )"
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );
  REQUIRE(
    NodeGet<SpotterEvent>(node, "event")
    == SpotterEvent{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"}},
      {{.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"}},
    }
  );
  REQUIRE(
    NodeGet<SpotterEvent>(node, "event")
    == SpotterEvent{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"},
       {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"}},
    }
  );
  REQUIRE(
    NodeGet<SpotterEvent>(node, "event")
    == SpotterEvent{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }
  );
}

namespace giga_edge {

inline bool operator==(const FilterEventsNodeParams& lhs, const FilterEventsNodeParams& rhs) {
  return lhs.event_types == rhs.event_types;
}

} // namespace giga_edge

TEST_CASE("FilterEvents, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<FilterEventsNodeParams>(YAML::Load(
      "event_types:\n"
      "  - activation\n"
      "  - quick_command\n"
    ))
    == FilterEventsNodeParams{.event_types = {SpotterEventType::ACTIVATION, SpotterEventType::QUICK_COMMAND}}
  );
}
