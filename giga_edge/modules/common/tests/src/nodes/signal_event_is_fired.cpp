#include "giga_edge/lib/common/events.h"
#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/signal_event_is_fired/node.h"

#include <catch.hpp>

#include <stdexcept>

using namespace giga_edge;

namespace {

void FireActivationSignal(NodeContext& node) {
  NodePushEvent(node, Event{.event = SignalEvent{.signal_name = "activation"}});
}

} // namespace

TEST_CASE("Signal event is fired node", "[runtime][nodes]") {
  auto node = CreateNode(
    "signal_event_is_fired",
    R"(
      signal_name: activation
    )",
    {},
    {},
    {"input"}
  );

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePushEvent(node, Event{.event = SignalEvent{.signal_name = "other_signal"}});
  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePushEvent(node, Event{.event = SignalEvent{.signal_name = "activation"}});
  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Signal event is fired node resets fired signal with reset strategy", "[runtime][nodes]") {
  auto node = CreateNode(
    "signal_event_is_fired",
    R"(
      signal_name: activation
      reset_strategy: reset_signal
    )",
    {},
    {},
    {"input"}
  );

  FireActivationSignal(node);
  node.node->Reset();

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Signal event is fired node can ignore reset", "[runtime][nodes]") {
  auto node = CreateNode(
    "signal_event_is_fired",
    R"(
      signal_name: activation
      reset_strategy: ignore_reset
    )",
    {},
    {},
    {"input"}
  );

  FireActivationSignal(node);
  node.node->Reset();

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Signal event is fired node config parser", "[runtime][nodes]") {
  auto default_params = ParseConfig<SignalEventIsFiredNodeParams>(YAML::Load("signal_name: activation\n"));
  REQUIRE(default_params.signal_name == "activation");
  REQUIRE(default_params.reset_strategy == SignalEventIsFiredResetStrategy::RESET_SIGNAL);

  auto reset_signal_params = ParseConfig<SignalEventIsFiredNodeParams>(YAML::Load(
    "signal_name: activation\n"
    "reset_strategy: reset_signal\n"
  ));
  REQUIRE(reset_signal_params.signal_name == "activation");
  REQUIRE(reset_signal_params.reset_strategy == SignalEventIsFiredResetStrategy::RESET_SIGNAL);

  auto ignore_reset_params = ParseConfig<SignalEventIsFiredNodeParams>(YAML::Load(
    "signal_name: activation\n"
    "reset_strategy: ignore_reset\n"
  ));
  REQUIRE(ignore_reset_params.signal_name == "activation");
  REQUIRE(ignore_reset_params.reset_strategy == SignalEventIsFiredResetStrategy::IGNORE_RESET);

  REQUIRE_THROWS_AS(
    ParseConfig<SignalEventIsFiredNodeParams>(YAML::Load(
      "signal_name: activation\n"
      "reset_strategy: unexpected\n"
    )),
    std::runtime_error
  );
}
