#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"
#include "giga_edge/modules/spotter/nodes/reference/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Reference node, no reference activation", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
        reference_timer: [1]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
    }
  );
}

TEST_CASE("Reference node, reference subactivation", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [1]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );
  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"}},
    }
  );
}

TEST_CASE("Reference node, reference activation", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [1]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );
  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"}},
    }
  );
}

TEST_CASE("Reference node, timer, multiple batches", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [3]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::NONE, .model_name = "events"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"}},
    }
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::NONE, .model_name = "events"}},
    }
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"}},
    }
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
    }
  );
}

TEST_CASE("Reference node, timer, single batch", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [3]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"}},
      {{.event_type = SpotterEventType::NONE, .model_name = "reference"}},
      {{.event_type = SpotterEventType::NONE, .model_name = "reference"}},
      {{.event_type = SpotterEventType::NONE, .model_name = "reference"}},
    }
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::NONE, .model_name = "events"}},
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
    }
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::NONE, .model_name = "events"}},
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::ACTIVATION, .model_name = "events"}},
      {{.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"}},
    }
  );
}

TEST_CASE("Reference node, multiple classes", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [1, 2, 1]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::NONE, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"},
        {.event_type = SpotterEventType::NONE, .model_name = "events"},
      },
      {
        {.event_type = SpotterEventType::NONE, .model_name = "reference"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
        {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      }
    }
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"},
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      },
      {
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
        {.event_type = SpotterEventType::NONE, .model_name = "reference"},
        {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      }
    }
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
      {.event_type = SpotterEventType::NONE, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );
}

TEST_CASE("Reference node, multiple reference activations", "[runtime][nodes]") {
  auto node = CreateNode(
    "reference",
    R"(
      reference_timer: [1, 1, 1]
    )"
  );

  NodePush(
    node,
    "reference_events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "reference"},
    }}
  );
  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
      {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
    }}
  );

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {
        {.event_type = SpotterEventType::ACTIVATION, .model_name = "events"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "events#reference"},
      },
      {
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
        {.event_type = SpotterEventType::SUBACTIVATION, .model_name = "reference"},
      }
    }
  );
}