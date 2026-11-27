#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/timer/node.h"

#include <catch.hpp>

#include <thread>

using namespace giga_edge;

TEST_CASE("Timer node ticks, 0 triggers immediately", "[runtime][nodes]") {
  auto node = CreateNode(
    "timer",
    R"(
      timer: 0
      restart_strategy: restart
    )",
    {},
    {"alarm", "start"}
  );

  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);
}

TEST_CASE("Timer node ticks, triggers after N ticks", "[runtime][nodes]") {
  auto node = CreateNode(
    "timer",
    R"(
      timer: 3
      restart_strategy: restart
    )",
    {},
    {"alarm", "start"}
  );

  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);
}

TEST_CASE("Timer node ticks, restart strategy restarts counter", "[runtime][nodes]") {
  auto node = CreateNode(
    "timer",
    R"(
      timer: 3
      restart_strategy: restart
    )",
    {},
    {"alarm", "start"}
  );

  // Trigger the timer.
  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  // Trigger the timer a second time.
  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);
}

TEST_CASE("Timer node ticks, restart strategy ignore", "[runtime][nodes]") {
  auto node = CreateNode(
    "timer",
    R"(
      timer: 4
      restart_strategy: ignore
    )",
    {},
    {"alarm", "start"}
  );

  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  // Pushing start: the counter should not be dropped.
  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);
}

TEST_CASE("Timer node continuous", "[runtime][nodes]") {
  auto node = CreateNode(
    "timer",
    R"(
      timer: 2
      continuous_alarm: true
    )",
    {},
    {"alarm", "start"}
  );

  NodePush(node, "start", true);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == true);

  NodePush(node, "start", false);
  REQUIRE(NodeGet<bool>(node, "alarm") == false);
}
