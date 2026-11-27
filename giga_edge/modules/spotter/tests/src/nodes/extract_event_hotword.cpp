#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/events/tests/events_test_utils/compare.h"
#include "giga_edge/modules/spotter/nodes/extract_event_hotword/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Extract event hotword node, activation", "[runtime][nodes]") {
  auto node = CreateNode(
    "extract_event_hotword",
    R"(
      event_type: activation
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

  REQUIRE(NodeGet<std::string>(node, "hotword") == "1");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword") == "1");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "1"},
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "2"},
      {.event_type = SpotterEventType::QUICK_COMMAND, .hot_word = "3"},
    }}
  );

  REQUIRE(NodeGet<std::string>(node, "hotword").empty());
}

TEST_CASE("Extract event hotword node, quick_command", "[runtime][nodes]") {
  auto node = CreateNode(
    "extract_event_hotword",
    R"(
      event_type: quick_command
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

  REQUIRE(NodeGet<std::string>(node, "hotword") == "3");
}

TEST_CASE("Extract event hotword node, asr variable", "[runtime][nodes]") {
  auto node = CreateNode(
    "extract_event_hotword",
    R"(
      event_type: activation
      asr_variable: custom_spotter
    )"
  );

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::SUBACTIVATION, .hot_word = "1", .asr_variables = {{"custom_spotter", "искра"}}},
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "2", .asr_variables = {{"custom_spotter", "джарвис"}}},
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "3", .asr_variables = {{"custom_spotter", "маша"}}},
    }}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword") == "джарвис");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "2", .asr_variables = {{"other", "джарвис"}}},
    }}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword").empty());

  NodePush(node, "events", std::vector<SpotterEvent>{});
  REQUIRE(NodeGet<std::string>(node, "hotword").empty());
}

TEST_CASE("Extract event hotword node, asr forms", "[runtime][nodes]") {
  auto node = CreateNode(
    "extract_event_hotword",
    R"(
      event_type: activation
      asr_variable: custom_spotter
      asr_forms: {hotword_1: ветер, hotword_2: маяк}
    )"
  );

  NodePush(
    node, "events", std::vector<SpotterEvent>{{{.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_1"}}}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword") == "ветер");

  NodePush(
    node,
    "events",
    std::vector<SpotterEvent>{{
      {.event_type = SpotterEventType::ACTIVATION, .hot_word = "any", .asr_variables = {{"custom_spotter", "джарвис"}}},
    }}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword") == "джарвис");

  NodePush(
    node, "events", std::vector<SpotterEvent>{{{.event_type = SpotterEventType::ACTIVATION, .hot_word = "hotword_4"}}}
  );
  REQUIRE(NodeGet<std::string>(node, "hotword").empty());
}

namespace giga_edge {

inline bool operator==(const ExtractEventHotwordNodeParams& lhs, const ExtractEventHotwordNodeParams& rhs) {
  return lhs.event_type == rhs.event_type && lhs.asr_variable == rhs.asr_variable && lhs.asr_forms == rhs.asr_forms;
}

} // namespace giga_edge

TEST_CASE("ExtractEventHotword, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<ExtractEventHotwordNodeParams>(YAML::Load("event_type: quick_command\n"))
    == ExtractEventHotwordNodeParams{.event_type = SpotterEventType::QUICK_COMMAND}
  );

  REQUIRE(
    ParseConfig<ExtractEventHotwordNodeParams>(YAML::Load(""))
    == ExtractEventHotwordNodeParams{.event_type = SpotterEventType::ACTIVATION}
  );

  REQUIRE(
    ParseConfig<ExtractEventHotwordNodeParams>(YAML::Load("asr_variable: custom_spotter\n"))
    == ExtractEventHotwordNodeParams{.event_type = SpotterEventType::ACTIVATION, .asr_variable = "custom_spotter"}
  );

  REQUIRE(
    ParseConfig<ExtractEventHotwordNodeParams>(YAML::Load("asr_forms: {hotword_1: ветер}\n"))
    == ExtractEventHotwordNodeParams{.event_type = SpotterEventType::ACTIVATION, .asr_forms = {{"hotword_1", "ветер"}}}
  );
}
