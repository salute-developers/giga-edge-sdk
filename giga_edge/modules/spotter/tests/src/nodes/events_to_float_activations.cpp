#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/spotter/nodes/events_to_float_activations/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Events to float activations node, single event", "[runtime][nodes]") {
  auto node = CreateNode("events_to_float_activations", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::NONE},
      SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
    }}
  );
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "floats"), Catch::Approx(std::vector<float>{1., 0., 0., 0.}).epsilon(1.e-5)
  );
}

TEST_CASE("Events to float activations node, multiple events", "[runtime][nodes]") {
  auto node = CreateNode("events_to_float_activations", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
        SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
      },
      {
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
        SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
      },
      {
        SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
      }
    }
  );
  REQUIRE_THAT(
    NodeGet<std::vector<float>>(node, "floats"), Catch::Approx(std::vector<float>{1., 1., 0., 1.}).epsilon(1.e-5)
  );
}