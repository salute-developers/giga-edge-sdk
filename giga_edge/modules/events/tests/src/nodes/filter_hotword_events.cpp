#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/nodes/filter_hotword_events/node.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"

#include <catch.hpp>

using namespace giga_edge;

namespace giga_edge {

inline bool operator==(const FilterHotwordEventsNodeParams& lhs, const FilterHotwordEventsNodeParams& rhs) {
  return lhs.hotwords == rhs.hotwords;
}

} // namespace giga_edge

TEST_CASE("filter_hotword_events node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<FilterHotwordEventsNodeParams>(YAML::Load("hotwords: [ hotword_1, hotword_3, hotword_2 ]\n"))
    == FilterHotwordEventsNodeParams{.hotwords = {"hotword_1", "hotword_3", "hotword_2"}}
  );

  REQUIRE(
    ParseConfig<FilterHotwordEventsNodeParams>(YAML::Load(
      "hotwords:\n"
      "  - hotword_1\n"
      "  - hotword_3\n"
      "  - hotword_2\n"
    ))
    == FilterHotwordEventsNodeParams{.hotwords = {"hotword_1", "hotword_3", "hotword_2"}}
  );
}

TEST_CASE("filter_hotword_events node, single hotword", "[runtime][nodes]") {
  auto node = CreateNode(
    "filter_hotword_events",
    R"(
      hotwords: [ "hotword_2" ]
    )"
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_2"},
      },
    }
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "filtered_events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_2"},
      },
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
    }
  );
  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "filtered_events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
      },
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
    }
  );
  REQUIRE(NodeGet<std::vector<SpotterEvent>>(node, "filtered_events").empty());
}

TEST_CASE("filter_hotword_events node, multiple hotwords", "[runtime][nodes]") {
  auto node = CreateNode(
    "filter_hotword_events",
    R"(
      hotwords: [ "hotword_2", "hotword_1" ]
    )"
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_2"},
      },
    }
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "filtered_events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_2"},
      },
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
    }
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "filtered_events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_2"},
      },
    }
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
      {
        {.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_3"},
        {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "hotword_3"},
      },
    }
  );
  REQUIRE(NodeGet<std::vector<SpotterEvent>>(node, "filtered_events").empty());
}