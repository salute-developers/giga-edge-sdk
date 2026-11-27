#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/spotter/nodes/has_activation/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Has activation node, no activation", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      SingleSpotterEvent{.event_type = SpotterEventType::NONE},
      SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
    }}
  );
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Has activation node, single activation", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::NONE},
      SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
    }}
  );
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Has activation node, multiple activations", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::NONE},
    }}
  );
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Has activation node, mixed event types", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
      SingleSpotterEvent{.event_type = SpotterEventType::NONE},
      SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
      SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
    }}
  );
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Has activation node, empty events", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(node, "events", std::vector<SpotterEvent>{});
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Has activation node, multiple event batches", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
        SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
      },
      {
        SingleSpotterEvent{.event_type = SpotterEventType::ACTIVATION},
        SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
      }
    }
  );
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

TEST_CASE("Has activation node, no activation in multiple batches", "[runtime][nodes]") {
  auto node = CreateNode("has_activation", "");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
        SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION},
      },
      {
        SingleSpotterEvent{.event_type = SpotterEventType::DISABLED},
        SingleSpotterEvent{.event_type = SpotterEventType::NONE},
      }
    }
  );
  REQUIRE(NodeGet<bool>(node, "output") == false);
}
