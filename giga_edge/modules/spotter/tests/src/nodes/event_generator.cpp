#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"
#include "giga_edge/modules/spotter/nodes/event_generator/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Event generator node, activation_modes", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: all
      model_name: "test"
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
        - hotword: "hotword_2"
          ru_hotword: "маяк"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
        - hotword: "hotword_3"
          ru_hotword: "искра"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
    )"
  );

  {
    INFO("ActivationMode::ALL");
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {{.event_type = SpotterEventType::SUBACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 0.6,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}}
      }
    );
  }

  {
    INFO("ActivationMode::MAIN");
    NodePushEvent(
      node,
      Event{
        .event = SetActivationModeEvent{
          .model_name = "all",
          .activaion_mode = ActivationMode::MAIN,
        }
      }
    );

    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("ActivationMode::MAIN_AND_ACTIVE");
    NodePushEvent(
      node,
      Event{
        .event = SetActivationModeEvent{
          .model_name = "all",
          .activaion_mode = ActivationMode::MAIN_AND_ACTIVE,
        },
      }
    );
    NodePushEvent(
      node,
      Event{
        .event = SetActiveNameEvent{
          .active_name = "hotword_2",
        },
      }
    );
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }
}

TEST_CASE("Event generator node, set and reset synthesis text with delay", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: main_and_active
      model_name: "test"
      reset_synthesis_text_delay: 1
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
        - hotword: "hotword_2"
          ru_hotword: "маяк"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
        - hotword: "hotword_3"
          ru_hotword: "искра"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
    )"
  );

  NodePushEvent(
    node,
    Event{
      .event = SetActivationModeEvent{
        .model_name = "all",
        .activaion_mode = ActivationMode::MAIN_AND_ACTIVE,
      }
    }
  );

  NodePushEvent(
    node,
    Event{
      .event = SetActiveNameEvent{
        .active_name = "hotword_2",
      }
    }
  );

  {
    INFO("No synthesis text");
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Set synthesis text and reset with delay");

    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите маяк"}});

    NodePushEvent(
      node,
      Event{
        .event = ResetSynthesisTextEvent{},
      }
    );

    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});

    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );

    // Delay is ended -> reset is done.
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }
}

TEST_CASE("Event generator node, synthesis text", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: main_and_active
      model_name: "test"
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
        - hotword: "hotword_2"
          ru_hotword: "маяк"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
        - hotword: "hotword_3"
          ru_hotword: "искра"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
    )"
  );
  NodePushEvent(
    node,
    Event{
      .event = SetActivationModeEvent{
        .model_name = "all",
        .activaion_mode = ActivationMode::MAIN_AND_ACTIVE,
      }
    }
  );
  NodePushEvent(
    node,
    Event{
      .event = SetActiveNameEvent{
        .active_name = "hotword_2",
      }
    }
  );

  {
    INFO("No synthesis text");
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Synthesis text has one hotword, activation should change to disabled");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите маяк"}});
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Synthesis text has one hotword, subactivation should change to none");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите ветер"}});
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::DISABLED,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::NONE,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Synthesis text has two hotwords, both should become disabled");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите маяк или ветер"}});
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::DISABLED,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::NONE,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Reset synthesis text");
    NodePushEvent(
      node,
      Event{
        .event = ResetSynthesisTextEvent{},
      }
    );
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }

  {
    INFO("Synthesis text has hotword substring but no hotword, should disable nothing");
    NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите маякбум"}});
    NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});
    REQUIRE(
      NodeGet<std::vector<SpotterEvent>>(node, "events")
      == std::vector<SpotterEvent>{
        {{.event_type = SpotterEventType::ACTIVATION,
          .hot_word = "hotword_1",
          .hot_word_length_ms = 1200,
          .score = 1.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_2",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER},
         {.event_type = SpotterEventType::NONE,
          .hot_word = "hotword_3",
          .hot_word_length_ms = 1200,
          .score = 0.0,
          .model_name = "test",
          .spotter_engine = SpotterEngineType::SPOTTER}},
        {
          {.event_type = SpotterEventType::SUBACTIVATION,
           .hot_word = "hotword_1",
           .hot_word_length_ms = 1200,
           .score = 0.6,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::ACTIVATION,
           .hot_word = "hotword_2",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
          {.event_type = SpotterEventType::DISABLED,
           .hot_word = "hotword_3",
           .hot_word_length_ms = 1200,
           .score = 1.0,
           .model_name = "test",
           .spotter_engine = SpotterEngineType::SPOTTER},
        }
      }
    );
  }
}

TEST_CASE("Event generator node, channels", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: main
      model_name: "test"
      reset_synthesis_text_delay: 1
      channels: ["asr"]
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
    )"
  );

  NodePush(node, "predictions", std::vector<float>{1.0, 0.6, 0.0});
  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 1.0,
        .model_name = "test",
        .channels = {"asr"},
        .spotter_engine = SpotterEngineType::SPOTTER}},
      {{.event_type = SpotterEventType::SUBACTIVATION,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 0.6,
        .model_name = "test",
        .channels = {"asr"},
        .spotter_engine = SpotterEngineType::SPOTTER}},
      {{.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 0.0,
        .model_name = "test",
        .channels = {"asr"},
        .spotter_engine = SpotterEngineType::SPOTTER}},
    }
  );
}

TEST_CASE("Event generator node ov_threshold", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: all
      model_name: "test"
      reset_synthesis_text_delay: 1
      channels: ["asr"]
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
          ov_threshold: 0.5
        - hotword: "hotword_2"
          ru_hotword: "маяк"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
          ov_threshold: 0.7
    )"
  );

  NodePush(node, "predictions", std::vector<float>{1.0, 0.1, 0.1, 1.0});
  auto result = NodeGet<std::vector<SpotterEvent>>(node, "events");
  REQUIRE(
    result
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 1.0,
        .model_name = "test",
        .channels = {"asr"},
        .ov_threshold = 0.5,
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_2",
        .hot_word_length_ms = 1200,
        .score = 0.1,
        .model_name = "test",
        .channels = {"asr"},
        .ov_threshold = 0.7,
        .spotter_engine = SpotterEngineType::SPOTTER}},
      {{.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 0.1,
        .model_name = "test",
        .channels = {"asr"},
        .ov_threshold = 0.5,
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::ACTIVATION,
        .hot_word = "hotword_2",
        .hot_word_length_ms = 1200,
        .score = 1.0,
        .model_name = "test",
        .channels = {"asr"},
        .ov_threshold = 0.7,
        .spotter_engine = SpotterEngineType::SPOTTER}},
    }
  );
}

TEST_CASE("Test asr_variables map field", "[runtime][nodes]") {
  // Equal case.
  {
    SingleSpotterEvent event_1;
    {
      event_1.event_type = SpotterEventType::NONE;
      event_1.hot_word = "hotword_2";
      event_1.hot_word_length_ms = 1200;
      event_1.model_name = "model";
      event_1.score = 0.9;
      event_1.ov_threshold = 0.7;
      event_1.asr_variables = {{"spotter_variable", "value_1"}};
      event_1.spotter_engine = SpotterEngineType::SPOTTER;
    }

    SingleSpotterEvent event_2;
    {
      event_2.event_type = SpotterEventType::NONE;
      event_2.hot_word = "hotword_2";
      event_2.hot_word_length_ms = 1200;
      event_2.model_name = "model";
      event_2.score = 0.9;
      event_2.ov_threshold = 0.7;
      event_2.asr_variables = {{"spotter_variable", "value_1"}};
      event_2.spotter_engine = SpotterEngineType::SPOTTER;
    }

    REQUIRE((event_1 == event_2));
  }

  // Not equal case (different asr_variables).
  {
    SingleSpotterEvent event_1;
    {
      event_1.event_type = SpotterEventType::NONE;
      event_1.hot_word = "hotword_2";
      event_1.hot_word_length_ms = 1200;
      event_1.model_name = "model";
      event_1.score = 0.9;
      event_1.ov_threshold = 0.7;
      event_1.asr_variables = {{"spotter_variable", "value_1"}};
    }

    SingleSpotterEvent event_2;
    {
      event_2.event_type = SpotterEventType::NONE;
      event_2.hot_word = "hotword_2";
      event_2.hot_word_length_ms = 1200;
      event_2.model_name = "model";
      event_2.score = 0.9;
      event_2.ov_threshold = 0.7;
      event_2.asr_variables = {{"spotter_variable", "value_23"}};
    }

    REQUIRE(!(event_1 == event_2));
  }
}

TEST_CASE("Event generator node, synthesis text in params", "[runtime][nodes]") {
  auto node = CreateNode(
    "event_generator",
    R"(
      activation_mode: main_and_active
      model_name: "test"
      reset_synthesis_text_delay: 1
      ignore_synthesis_text: true
      classes:
        - hotword: "hotword_1"
          ru_hotword: "ветер"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: main
        - hotword: "hotword_2"
          ru_hotword: "маяк"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
        - hotword: "hotword_3"
          ru_hotword: "искра"
          hotword_length_ms: 1200
          sub_threshold: 0.5
          threshold: 0.9
          hotword_group: assistant
    )"
  );

  NodePushEvent(
    node,
    Event{
      .event = SetActiveNameEvent{
        .active_name = "hotword_2",
      }
    }
  );

  NodePushEvent(node, Event{.event = SetSynthesisTextEvent{.synthesis_text = "скажите маяк или ветер"}});

  NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 1.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_2",
        .hot_word_length_ms = 1200,
        .score = 0.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_3",
        .hot_word_length_ms = 1200,
        .score = 0.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER}},
      {
        {.event_type = SpotterEventType::SUBACTIVATION,
         .hot_word = "hotword_1",
         .hot_word_length_ms = 1200,
         .score = 0.6,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
        {.event_type = SpotterEventType::ACTIVATION,
         .hot_word = "hotword_2",
         .hot_word_length_ms = 1200,
         .score = 1.0,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
        {.event_type = SpotterEventType::DISABLED,
         .hot_word = "hotword_3",
         .hot_word_length_ms = 1200,
         .score = 1.0,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
      }
    }
  );

  NodePushEvent(
    node,
    Event{
      .event = ResetSynthesisTextEvent{},
    }
  );

  NodePush(node, "predictions", std::vector<float>{1.0, 0.0, 0.0, 0.6, 1.0, 1.0});

  REQUIRE(
    NodeGet<std::vector<SpotterEvent>>(node, "events")
    == std::vector<SpotterEvent>{
      {{.event_type = SpotterEventType::ACTIVATION,
        .hot_word = "hotword_1",
        .hot_word_length_ms = 1200,
        .score = 1.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_2",
        .hot_word_length_ms = 1200,
        .score = 0.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER},
       {.event_type = SpotterEventType::NONE,
        .hot_word = "hotword_3",
        .hot_word_length_ms = 1200,
        .score = 0.0,
        .model_name = "test",
        .spotter_engine = SpotterEngineType::SPOTTER}},
      {
        {.event_type = SpotterEventType::SUBACTIVATION,
         .hot_word = "hotword_1",
         .hot_word_length_ms = 1200,
         .score = 0.6,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
        {.event_type = SpotterEventType::ACTIVATION,
         .hot_word = "hotword_2",
         .hot_word_length_ms = 1200,
         .score = 1.0,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
        {.event_type = SpotterEventType::DISABLED,
         .hot_word = "hotword_3",
         .hot_word_length_ms = 1200,
         .score = 1.0,
         .model_name = "test",
         .spotter_engine = SpotterEngineType::SPOTTER},
      }
    }
  );
}