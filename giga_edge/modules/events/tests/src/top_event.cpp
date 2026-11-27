#include "giga_edge/modules/events/top_event.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Top event: among events without a trigger the one with more payloads wins", "[events]") {
  const SpotterEvent bare{{.event_type = SpotterEventType::NONE, .score = 0.9F}};
  const SpotterEvent recognized{{.event_type = SpotterEventType::NONE, .score = 0.1F, .payload = 1}};

  REQUIRE(GetTopPriorityEvent({bare, recognized})[0].payload.has_value());
  REQUIRE(GetTopPriorityEvent({recognized, bare})[0].payload.has_value());
}

TEST_CASE("Top event: payload does not rank events with a trigger", "[events]") {
  const SpotterEvent low{{.event_type = SpotterEventType::ACTIVATION, .score = 0.1F, .payload = 1}};
  const SpotterEvent high{{.event_type = SpotterEventType::ACTIVATION, .score = 0.9F}};

  REQUIRE_FALSE(GetTopPriorityEvent({low, high})[0].payload.has_value());
}
