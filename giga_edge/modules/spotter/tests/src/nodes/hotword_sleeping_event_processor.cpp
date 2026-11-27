#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/spotter/nodes/hotword_sleeping_event_processor/node.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

SingleSpotterEvent MakeActivation(std::string hotword) {
  return SingleSpotterEvent{
    .event_type = SpotterEventType::ACTIVATION,
    .hot_word = std::move(hotword),
  };
}

SingleSpotterEvent MakeDisabled(std::string hotword) {
  return SingleSpotterEvent{
    .event_type = SpotterEventType::DISABLED,
    .hot_word = std::move(hotword),
  };
}

std::vector<SpotterEvent> Push(NodeContext& node, std::vector<SpotterEvent> events) {
  NodePush(node, "events", events);
  return NodeGet<std::vector<SpotterEvent>>(node, "events");
}

} // namespace

TEST_CASE("HotwordSleepingEventProcessor: activation passes through when no recent activation", "[runtime][nodes]") {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 15
      default_timer: 10
    )"
  );

  const auto out = Push(node, {{MakeActivation("hotword_1")}});
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].size() == 1);
  REQUIRE(out[0][0].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(out[0][0].hot_word == "hotword_1");
}

TEST_CASE(
  "HotwordSleepingEventProcessor: second activation within timer is demoted to subactivation", "[runtime][nodes]"
) {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 3
      default_timer: 10
    )"
  );

  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::SUBACTIVATION);
  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::SUBACTIVATION);
  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::SUBACTIVATION);
}

TEST_CASE("HotwordSleepingEventProcessor: per-hotword timers are independent", "[runtime][nodes]") {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 15
        hotword_4: 3
      default_timer: 10
    )"
  );

  const auto out1 = Push(node, {{MakeActivation("hotword_1"), MakeActivation("hotword_4")}});
  REQUIRE(out1[0][0].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(out1[0][0].hot_word == "hotword_1");
  REQUIRE(out1[0][1].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(out1[0][1].hot_word == "hotword_4");

  const auto out2 = Push(node, {{MakeActivation("hotword_1"), MakeActivation("hotword_4")}});
  REQUIRE(out2[0][0].event_type == SpotterEventType::SUBACTIVATION);
  REQUIRE(out2[0][1].event_type == SpotterEventType::SUBACTIVATION);

  for (int i = 0; i < 2; ++i) {
    Push(node, {{}});
  }
  const auto out3 = Push(node, {{MakeActivation("hotword_1"), MakeActivation("hotword_4")}});
  REQUIRE(out3[0][0].event_type == SpotterEventType::SUBACTIVATION);
  REQUIRE(out3[0][1].event_type == SpotterEventType::ACTIVATION);
}

TEST_CASE(
  "HotwordSleepingEventProcessor: hotwords absent from timers map share default value but not state", "[runtime][nodes]"
) {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 15
      default_timer: 10
    )"
  );

  const auto out1 = Push(node, {{MakeActivation("kapitan")}});
  REQUIRE(out1[0][0].event_type == SpotterEventType::ACTIVATION);

  const auto out2 = Push(node, {{MakeActivation("superman")}});
  REQUIRE(out2[0][0].event_type == SpotterEventType::ACTIVATION);

  const auto out3 = Push(node, {{MakeActivation("kapitan")}});
  REQUIRE(out3[0][0].event_type == SpotterEventType::SUBACTIVATION);

  const auto out4 = Push(node, {{MakeActivation("superman")}});
  REQUIRE(out4[0][0].event_type == SpotterEventType::SUBACTIVATION);
}

TEST_CASE("HotwordSleepingEventProcessor: timer ticks per node call, not per single-event count", "[runtime][nodes]") {
  // The timer must not be decremented per single event in the batch.
  // Pushing one big batch of N activations should consume only ONE tick.
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 5
      default_timer: 10
    )"
  );

  const auto out1 = Push(
    node,
    {{
      MakeActivation("hotword_1"),
      MakeActivation("hotword_1"),
      MakeActivation("hotword_1"),
      MakeActivation("hotword_1"),
      MakeActivation("hotword_1"),
      MakeActivation("hotword_1"),
    }}
  );
  REQUIRE(out1[0][0].event_type == SpotterEventType::ACTIVATION);
  for (size_t i = 1; i < 6; ++i) {
    REQUIRE(out1[0][i].event_type == SpotterEventType::SUBACTIVATION);
  }

  // After 4 empty calls the timer must expire. With a per-single-event tick
  // the 6 events in the previous call would have already drained it.
  for (int i = 0; i < 4; ++i) {
    Push(node, {{}});
  }
  const auto out2 = Push(node, {{MakeActivation("hotword_1")}});
  REQUIRE(out2[0][0].event_type == SpotterEventType::ACTIVATION);
}

TEST_CASE("HotwordSleepingEventProcessor: DISABLED is converted to NONE while timer is running", "[runtime][nodes]") {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 5
      default_timer: 10
    )"
  );

  const auto out1 = Push(node, {{MakeDisabled("hotword_1")}});
  REQUIRE(out1[0][0].event_type == SpotterEventType::DISABLED);

  const auto out2 = Push(node, {{MakeDisabled("hotword_1")}});
  REQUIRE(out2[0][0].event_type == SpotterEventType::NONE);

  // Timer is shared between ACTIVATION/DISABLED for the same hotword, so an
  // activation arriving while a DISABLED-armed timer is running gets demoted too.
  const auto out3 = Push(node, {{MakeActivation("hotword_1")}});
  REQUIRE(out3[0][0].event_type == SpotterEventType::SUBACTIVATION);
}

TEST_CASE("HotwordSleepingEventProcessor: Reset clears all timers", "[runtime][nodes]") {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 100
      default_timer: 100
    )"
  );

  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::ACTIVATION);
  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::SUBACTIVATION);

  node.node->Reset();

  REQUIRE(Push(node, {{MakeActivation("hotword_1")}})[0][0].event_type == SpotterEventType::ACTIVATION);
}

TEST_CASE("HotwordSleepingEventProcessor: NONE/SUBACTIVATION events do not arm the timer", "[runtime][nodes]") {
  auto node = CreateNode(
    "hotword_sleeping_event_processor",
    R"(
      timers:
        hotword_1: 5
      default_timer: 10
    )"
  );

  Push(
    node,
    {{
      SingleSpotterEvent{.event_type = SpotterEventType::NONE, .hot_word = "hotword_1"},
      SingleSpotterEvent{.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "hotword_1"},
    }}
  );

  const auto out = Push(node, {{MakeActivation("hotword_1")}});
  REQUIRE(out[0][0].event_type == SpotterEventType::ACTIVATION);
}

namespace giga_edge {

inline bool operator==(const HotwordSleepingEventProcessorParams& lhs, const HotwordSleepingEventProcessorParams& rhs) {
  return lhs.timers == rhs.timers && lhs.default_timer == rhs.default_timer;
}

} // namespace giga_edge

TEST_CASE("HotwordSleepingEventProcessor: config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<HotwordSleepingEventProcessorParams>(YAML::Load(R"(
      timers:
        hotword_1: 15
        hotword_4: 3
      default_timer: 10
    )"))
    == HotwordSleepingEventProcessorParams{
      .timers = {{"hotword_1", 15}, {"hotword_4", 3}},
      .default_timer = 10,
    }
  );
}

TEST_CASE("HotwordSleepingEventProcessor: config parser, default_timer omitted", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<HotwordSleepingEventProcessorParams>(YAML::Load(R"(
      timers:
        hotword_1: 15
    )"))
    == HotwordSleepingEventProcessorParams{
      .timers = {{"hotword_1", 15}},
      .default_timer = 0,
    }
  );
}

TEST_CASE("HotwordSleepingEventProcessor: config parser, timers omitted", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<HotwordSleepingEventProcessorParams>(YAML::Load(R"(
      default_timer: 7
    )"))
    == HotwordSleepingEventProcessorParams{
      .timers = {},
      .default_timer = 7,
    }
  );
}
