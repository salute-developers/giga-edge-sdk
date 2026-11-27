#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("user_query_from_text numbers requests and skips empty ticks", "[assistant][node][input]") {
  auto ctx = CreateNode("user_query_from_text", "request_id_prefix: q-");
  NodePush<std::string>(ctx, "text", "");
  auto query = NodeGet<UserQuery>(ctx, "user_query");
  REQUIRE(query.status == AssistantStatus::SKIPPED);
  REQUIRE(query.request_id.empty());

  NodePush<std::string>(ctx, "text", "hi");
  query = NodeGet<UserQuery>(ctx, "user_query");
  REQUIRE(query.status == AssistantStatus::OK);
  REQUIRE(query.request_id == "q-0");
  REQUIRE(query.text == "hi");

  NodePush<std::string>(ctx, "text", "again");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").request_id == "q-1");

  NodePush<std::string>(ctx, "text", " включи лампу\n");
  query = NodeGet<UserQuery>(ctx, "user_query");
  REQUIRE(query.text == "включи лампу");
  REQUIRE(query.request_id == "q-2");

  NodePush<std::string>(ctx, "text", "  ");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").status == AssistantStatus::SKIPPED);

  NodePushEvent(ctx, Event{.event = ResetEvent{}});
  NodePush<std::string>(ctx, "text", "after reset");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").request_id == "q-0");
}

TEST_CASE("scene_snapshot_from_text versions the scene by its text", "[assistant][node][input]") {
  auto ctx = CreateNode("scene_snapshot_from_text", "scene_version: s\nallow_empty: false");
  NodePush<std::string>(ctx, "text", "lamp on");
  const auto a = NodeGet<SceneSnapshot>(ctx, "scene_snapshot");
  NodePush<std::string>(ctx, "text", "lamp off");
  const auto b = NodeGet<SceneSnapshot>(ctx, "scene_snapshot");
  REQUIRE(a.text == "lamp on");
  REQUIRE_FALSE(a.allow_empty);
  REQUIRE(a.version.rfind("s#", 0) == 0);
  REQUIRE(a.version != b.version);
}

TEST_CASE("tool_specs_from_json parses a host catalog", "[assistant][node][input]") {
  auto ctx = CreateNode("tool_specs_from_json", "");
  NodePush<std::string>(
    ctx, "json", R"([{"name":"ping","description":"check","parameters":{"type":"object"}},{"name":""}])"
  );
  auto specs = NodeGet<ToolSpecs>(ctx, "tool_specs");
  REQUIRE(specs.status == AssistantStatus::OK);
  REQUIRE(specs.tools.size() == 1);
  REQUIRE(specs.tools[0].name == "ping");
  REQUIRE(specs.tools[0].parameters_json == R"({"type":"object"})");
  REQUIRE_FALSE(specs.revision.empty());

  NodePush<std::string>(ctx, "json", "");
  specs = NodeGet<ToolSpecs>(ctx, "tool_specs");
  REQUIRE(specs.tools.empty());
  REQUIRE(specs.status == AssistantStatus::OK);

  NodePush<std::string>(ctx, "json", "{not an array}");
  specs = NodeGet<ToolSpecs>(ctx, "tool_specs");
  REQUIRE(specs.status == AssistantStatus::ERROR);
}

TEST_CASE("User query accepts an external request identity with generated fallback", "[assistant_core][nodes]") {
  auto node = CreateNode("user_query_from_text", "request_id_prefix: fallback-", {}, {"request_id"});
  NodePush(node, "text", std::string{"hello"});
  NodePush(node, "request_id", std::string{"client-42"});
  REQUIRE(NodeGet<UserQuery>(node, "user_query").request_id == "client-42");
  NodePush(node, "text", std::string{"again"});
  NodePush(node, "request_id", std::string{});
  REQUIRE(NodeGet<UserQuery>(node, "user_query").request_id == "fallback-0");
  NodePush(node, "text", std::string{});
  NodePush(node, "request_id", std::string{"unused"});
  REQUIRE(NodeGet<UserQuery>(node, "user_query").status == AssistantStatus::SKIPPED);
}
