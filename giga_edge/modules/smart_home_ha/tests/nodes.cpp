#include "giga_edge/modules/smart_home_ha/nodes.h"

#include "test_config.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
using namespace giga_edge;
using namespace giga_edge::ha_test;
namespace {
McpResolvedAction Tick(
  NodeContext& node,
  UserQuery query,
  const AssistantResult& result,
  const std::vector<McpResolvedAction>& next = {},
  bool reset = false,
  ha::CatalogPtr catalog = Catalog()
) {
  NodePush(node, "user_query", query);
  NodePush(node, "assistant_result", result);
  NodePush(node, "ha_catalog", catalog);
  NodePush(node, "continuations", next);
  NodePush(node, "dialogue_reset", reset);
  return NodeGet<McpResolvedAction>(node, "mcp_resolved_action");
}
UserQuery Query() {
  return {.request_id = "query-1", .text = "turn on the lamp"};
}
} // namespace
TEST_CASE("HA graph catalog feeds its compact scene node", "[ha][nodes]") {
  auto catalog = CreateNode("ha_catalog", "");
  auto scene = CreateNode("ha_scene", "");
  NodePush(catalog, "mcp_catalog_snapshot", Snapshot());
  const auto c = NodeGet<ha::CatalogPtr>(catalog, "ha_catalog");
  REQUIRE(c->devices.count("light_0") == 1);
  NodePush(scene, "ha_catalog", c);
  CHECK(NodeGet<SceneSnapshot>(scene, "scene_snapshot").text.find("Bedroom lamp") != std::string::npos);
}
TEST_CASE("HA catalog publishes the configured room of the assistant every tick", "[ha][nodes]") {
  std::ostringstream log;
  auto* const previous = std::cerr.rdbuf(log.rdbuf());
  for (const std::string room : {"Bedroom", "Bedrom"}) {
    auto node = CreateNode("ha_catalog", "current_room: " + room, {}, {}, {}, {}, nullptr, {"current_room"});
    for (int tick = 0; tick < 2; ++tick) {
      NodePush(node, "mcp_catalog_snapshot", Snapshot());
      CHECK(NodeGet<std::string>(node, "current_room") == room);
    }
  }
  std::cerr.rdbuf(previous);
  // A typo gives exactly one warning, a correct room gives none.
  const auto text = log.str();
  const auto typo = text.find("current_room 'Bedrom' is not an area");
  CHECK(typo != std::string::npos);
  CHECK(text.find("current_room 'Bedrom'", typo + 1) == std::string::npos);
  CHECK(text.find("'Bedroom'") == std::string::npos);
  auto unknown = CreateNode("ha_catalog", "", {}, {}, {}, {}, nullptr, {"current_room"});
  NodePush(unknown, "mcp_catalog_snapshot", Snapshot());
  CHECK(NodeGet<std::string>(unknown, "current_room").empty());
}
TEST_CASE("HA graph executes each final result once and accepts the tool-result generation", "[ha][nodes]") {
  auto node = CreateNode("ha_action_resolver", "");
  auto result = Action("set_device_parameter", "on_off", true);
  REQUIRE(Tick(node, Query(), result).tool_name == "homeassistant__GetLiveContext");
  CHECK(Tick(node, {}, result).status == AssistantStatus::SKIPPED);
  result.request_id = "query-1/tool/1";
  result.tool_calls[0].id = "query-1/tool/1/tool/1";
  REQUIRE(Tick(node, {}, result).tool_name == "homeassistant__GetLiveContext");
  CHECK(Tick(node, {}, result).status == AssistantStatus::SKIPPED);
}
TEST_CASE("HA graph reset and new queries discard delayed writes", "[ha][nodes]") {
  for (const bool reset : {false, true}) {
    for (const bool absolute : {false, true}) {
      auto node = CreateNode("ha_action_resolver", "");
      const auto result = absolute
        ? Action("set_device_parameter", "on_off", true)
        : Action("relative_change", "light_brightness", "+10%");
      const auto read = Tick(node, Query(), result);
      REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
      const auto next = ha::Complete(Reply(read, ReadContext())).continuations;
      REQUIRE(next.size() == 1);
      const UserQuery new_query = reset ? UserQuery{} : UserQuery{.request_id = "new-query", .text = "stop"};
      CHECK(Tick(node, new_query, {}, next, reset).status == AssistantStatus::SKIPPED);
      CHECK(Tick(node, {}, {}, next).status == AssistantStatus::SKIPPED);
    }
  }
}
TEST_CASE("HA graph accepts a correlated continuation once and rejects a changed target", "[ha][nodes]") {
  auto node = CreateNode("ha_action_resolver", "");
  const auto read = Tick(node, Query(), Action("relative_change", "light_brightness", "+10%"));
  const auto next = ha::Complete(Reply(read, ReadContext())).continuations;
  REQUIRE(next.size() == 1);
  REQUIRE(Tick(node, {}, {}, next).tool_name == "light__HassLightSet");
  CHECK(Tick(node, {}, {}, next).status == AssistantStatus::SKIPPED);

  auto changed = std::make_shared<ha::Catalog>(*Catalog());
  changed->devices.at("light_0").name = "Different lamp";
  const auto second_read = Tick(node, Query(), Action("relative_change", "light_brightness", "+10%"));
  const auto second_next = ha::Complete(Reply(second_read, ReadContext())).continuations;
  const auto rejected = Tick(node, {}, {}, second_next, false, changed);
  CHECK(rejected.status == AssistantStatus::ERROR);
  CHECK(rejected.kind == McpResolvedActionKind::NONE);
}
TEST_CASE("HA graph tool result feeds compact function result without losing correlation", "[ha][nodes]") {
  auto node = CreateNode("ha_tool_result", "");
  const auto action = WriteAction(Action("set_device_parameter", "on_off", true));
  NodePush(node, "mcp_completions", McpActionCompletions{Reply(action, Success())});
  const auto results = NodeGet<CompactToolResults>(node, "tool_results");
  REQUIRE(results.size() == 1);
  CHECK(results[0].call_id == "query-1/tool/1");
  CHECK_FALSE(results[0].is_error);
  CHECK(NodeGet<std::vector<McpResolvedAction>>(node, "continuations").empty());
}

namespace {
// MCP without hydration: the context resource is listed but not read, like a request right after HA connects.
struct UnreadContext {
  std::shared_ptr<mcp::McpServiceEnvironment> env = std::make_shared<mcp::McpServiceEnvironment>(
    mcp::McpServiceConfig{}
  );
  EnvironmentsMap environments{{"mcp", env}};
  std::shared_ptr<mcp::test_utils::FakeConnection> connection = std::make_shared<mcp::test_utils::FakeConnection>();
  NodeContext snapshot = CreateNode(
    "mcp_catalog_snapshot", "include_shared_latest_resource_states: true", {}, {}, {}, {}, &environments
  );
  NodeContext catalog;
  Json::Value read;

  explicit UnreadContext(const std::string& params)
    : catalog(CreateNode("ha_catalog", params, {}, {}, {}, {}, &environments)) {
    connection->EnqueueSuccess("initialize", ParseJsonOrThrow(R"({"protocolVersion":"2025-03-26",
      "serverInfo":{"name":"ha","version":"test"},"capabilities":{"tools":{},"resources":{}}})"));
    Json::Value tools, resources;
    for (const auto& t : Snapshot().tools) {
      Json::Value tool;
      tool["name"] = t.tool_name;
      tool["inputSchema"]["type"] = "object";
      tools["tools"].append(tool);
    }
    connection->EnqueueSuccess("tools/list", tools);
    Json::Value resource;
    resource["uri"] = "homeassistant://assist/context-snapshot";
    resource["name"] = "assist_context_snapshot";
    resource["mimeType"] = "text/plain";
    resources["resources"].append(resource);
    connection->EnqueueSuccess("resources/list", resources);
    resource["text"] = ParseJsonOrThrow(Snapshot().latest_resource_states[0].data_json)["text"].asString();
    read["contents"].append(resource);
  }
  ~UnreadContext() {
    env->service->RemoveServer("ha");
  }

  // Connects the server and returns the first snapshot where the resource is already listed.
  McpCatalogSnapshot Connect() {
    env->service->AddServer("ha", connection);
    McpCatalogSnapshot listed;
    REQUIRE(mcp::test_utils::WaitUntil([&] {
      listed = Tick();
      return !listed.resources.empty();
    }));
    REQUIRE(listed.latest_resource_states.empty());
    return listed;
  }
  McpCatalogSnapshot Tick() {
    NodePush(snapshot, "user_query", UserQuery{});
    return NodeGet<McpCatalogSnapshot>(snapshot, "mcp_catalog_snapshot");
  }
  ha::CatalogPtr Catalog(const McpCatalogSnapshot& s) {
    NodePush(catalog, "mcp_catalog_snapshot", s);
    return NodeGet<ha::CatalogPtr>(catalog, "ha_catalog");
  }
};
} // namespace
TEST_CASE("HA catalog reads a context that MCP has not read yet before the first answer", "[ha][nodes][refresh]") {
  for (const std::string params : {"context_wait_ms: 60000", "{refresh_interval_ms: 0, context_wait_ms: 60000}"}) {
    UnreadContext house(params);
    house.connection->EnqueueSuccess("resources/read", house.read);
    const auto c = house.Catalog(house.Connect());
    CHECK(c->status == AssistantStatus::OK);
    CHECK(c->devices.size() == 2);
  }
}
TEST_CASE("HA catalog waits for an unread context briefly and reads it later", "[ha][nodes][refresh]") {
  UnreadContext house("context_wait_ms: 100");
  house.connection->BlockMethod("resources/read");
  house.connection->EnqueueSuccess("resources/read", house.read);
  const auto listed = house.Connect();
  std::thread unblock([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    house.connection->UnblockMethod("resources/read");
  });
  const auto start = std::chrono::steady_clock::now();
  const auto first = house.Catalog(listed);
  const auto waited = std::chrono::steady_clock::now() - start;
  CHECK(waited < std::chrono::milliseconds(1500));
  CHECK(first->devices.empty());
  unblock.join();
  CHECK(mcp::test_utils::WaitUntil([&] {
    return house.Catalog(listed)->devices.size() == 2;
  }));
}
TEST_CASE("HA catalog does not wait for an unread context when the house is not empty", "[ha][nodes][refresh]") {
  UnreadContext house("context_wait_ms: 2000");
  house.connection->BlockMethod("resources/read");
  house.connection->EnqueueSuccess("resources/read", house.read);
  auto listed = house.Connect();
  // Another server already has the house; the "ha" context is being read, but there is no need to wait for it.
  for (auto tool : Snapshot().tools) {
    tool.server_id = "other";
    listed.tools.push_back(tool);
  }
  auto state = Snapshot().latest_resource_states[0];
  state.server_id = "other";
  listed.latest_resource_states.push_back(state);
  const auto start = std::chrono::steady_clock::now();
  const auto c = house.Catalog(listed);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1000));
  CHECK(c->devices.size() == 2);
  house.connection->UnblockMethod("resources/read");
}
namespace {
// Two HA servers in one MCP: "ha" replies with the context, "ha2" has no replies, so its read always fails.
struct TwoServers {
  UnreadContext house;
  UnreadContext second{""};
  McpCatalogSnapshot listed;

  explicit TwoServers(const std::string& params)
    : house(params) {
    for (int i = 0; i < 100; ++i) {
      house.connection->EnqueueSuccess("resources/read", house.read);
    }
    house.env->service->AddServer("ha", house.connection);
    house.env->service->AddServer("ha2", second.connection);
    REQUIRE(mcp::test_utils::WaitUntil([&] {
      listed = house.Tick();
      return listed.resources.size() == 2;
    }));
    REQUIRE(listed.latest_resource_states.empty());
  }
  ~TwoServers() {
    house.env->service->RemoveServer("ha2");
  }
  ha::CatalogPtr TickFor(std::chrono::milliseconds duration) {
    auto c = house.Catalog(listed);
    for (const auto until = std::chrono::steady_clock::now() + duration; std::chrono::steady_clock::now() < until;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      c = house.Catalog(listed);
    }
    return c;
  }
  static size_t Reads(const mcp::test_utils::FakeConnection& connection) {
    const auto calls = connection.RequestCalls();
    return std::count_if(calls.begin(), calls.end(), [](const auto& call) {
      return call.method == "resources/read";
    });
  }
};
} // namespace
TEST_CASE("HA catalog keeps a failing server from blocking the others and their refresh", "[ha][nodes][refresh]") {
  TwoServers servers("{refresh_interval_ms: 50, context_wait_ms: 200, context_retry_ms: 1}");
  const auto c = servers.TickFor(std::chrono::milliseconds(600));
  CHECK(c->devices.size() == 2);
  // A refresh every 50 ms over 600 ms: at least three and at most 1 + 600 / 50 reads.
  CHECK(TwoServers::Reads(*servers.house.connection) > 2);
  CHECK(TwoServers::Reads(*servers.house.connection) <= 15);
  CHECK(TwoServers::Reads(*servers.second.connection) > 2);
}
TEST_CASE("HA catalog retries only the unread context", "[ha][nodes][refresh]") {
  TwoServers servers("{refresh_interval_ms: 0, context_retry_ms: 1}");
  const auto c = servers.TickFor(std::chrono::milliseconds(200));
  CHECK(c->devices.size() == 2);
  CHECK(TwoServers::Reads(*servers.house.connection) == 1);
  CHECK(TwoServers::Reads(*servers.second.connection) > 1);
}
TEST_CASE("HA catalog reads the unread context only of its own servers", "[ha][nodes][refresh]") {
  TwoServers servers("{server_ids: [ha], refresh_interval_ms: 0, context_retry_ms: 1}");
  const auto c = servers.TickFor(std::chrono::milliseconds(200));
  CHECK(c->devices.size() == 2);
  CHECK(TwoServers::Reads(*servers.second.connection) == 0);
}
TEST_CASE("HA catalog reads a context again after a failed read", "[ha][nodes][refresh]") {
  UnreadContext house("{refresh_interval_ms: 0, context_retry_ms: 1}");
  house.connection->EnqueueError("resources/read", mcp::ErrorCode::INVALID_PARAMS, "unavailable");
  house.connection->EnqueueSuccess("resources/read", house.read);
  const auto listed = house.Connect();
  CHECK(house.Catalog(listed)->devices.empty());
  CHECK(mcp::test_utils::WaitUntil([&] {
    return house.Catalog(listed)->devices.size() == 2;
  }));
}
TEST_CASE("HA native catalog re-reads the context resource periodically", "[ha][nodes][refresh]") {
  mcp::McpServiceConfig config;
  config.resource_hydration.read_resources_on_list = true;
  config.resource_hydration.preferred_read_resource_match_phrases = {"homeassistant://assist/context-snapshot"};
  auto env = std::make_shared<mcp::McpServiceEnvironment>(config);
  EnvironmentsMap environments{{"mcp", env}};
  auto connection = std::make_shared<mcp::test_utils::FakeConnection>();
  connection->EnqueueSuccess("initialize", ParseJsonOrThrow(R"({"protocolVersion":"2025-03-26",
    "serverInfo":{"name":"ha","version":"test"},"capabilities":{"tools":{},"resources":{}}})"));
  Json::Value tools, resources;
  for (const auto& t : Snapshot().tools) {
    Json::Value tool;
    tool["name"] = t.tool_name;
    tool["inputSchema"]["type"] = "object";
    tools["tools"].append(tool);
  }
  connection->EnqueueSuccess("tools/list", tools);
  Json::Value resource;
  resource["uri"] = "homeassistant://assist/context-snapshot";
  resource["name"] = "assist_context_snapshot";
  resource["mimeType"] = "text/plain";
  resources["resources"].append(resource);
  connection->EnqueueSuccess("resources/list", resources);
  const auto text = ParseJsonOrThrow(Snapshot().latest_resource_states[0].data_json)["text"].asString();
  for (const auto& body : {text, text + "- names: Desk lamp\n  domain: light\n  state: 'off'\n"}) {
    Json::Value content;
    resource["text"] = body;
    content["contents"].append(resource);
    connection->EnqueueSuccess("resources/read", content);
  }
  auto snapshot = CreateNode(
    "mcp_catalog_snapshot", "include_shared_latest_resource_states: true", {}, {}, {}, {}, &environments
  );
  auto catalog = CreateNode("ha_catalog", "refresh_interval_ms: 1", {}, {}, {}, {}, &environments);
  env->service->AddServer("ha", connection);
  const auto tick = [&] {
    NodePush(snapshot, "user_query", UserQuery{});
    NodePush(catalog, "mcp_catalog_snapshot", NodeGet<McpCatalogSnapshot>(snapshot, "mcp_catalog_snapshot"));
    return NodeGet<ha::CatalogPtr>(catalog, "ha_catalog");
  };
  // The catalog sees the context already read by the hydration: otherwise the node reads it too, and the two reads get
  // the bodies in the order of the calls.
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    NodePush(snapshot, "user_query", UserQuery{});
    return !NodeGet<McpCatalogSnapshot>(snapshot, "mcp_catalog_snapshot").latest_resource_states.empty();
  }));
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    return tick()->devices.size() == 2;
  }));
  // Hydration read the initial lamp + climate; the refresh adds the desk lamp; later reads fail.
  ha::CatalogPtr c;
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    c = tick();
    return c->devices.size() == 3;
  }));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  for (int i = 0; i < 20; ++i) {
    c = tick();
  }
  CHECK(c->status == AssistantStatus::OK);
  CHECK(c->devices.size() == 3);
  env->service->RemoveServer("ha");
}

TEST_CASE("HA catalog shows online only when its config asks, as the demo graph does", "[ha][nodes][online]") {
  const auto graph = YAML::LoadFile(
    std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/giga_edge/modules/smart_home_ha/tests/data/function_calling/config.yaml"
  );
  YAML::Node demo, demo_scene;
  for (const auto& node : graph["nodes"]) {
    const auto name = node["name"].as<std::string>();
    if (name == "ha_catalog") {
      demo = node["params"];
    } else if (name == "ha_scene") {
      demo_scene = node["params"];
    }
  }
  REQUIRE(demo.IsMap());
  for (const auto& [config, online] : std::vector<std::pair<YAML::Node, bool>>{
         {YAML::Load("{}"), false}, {YAML::Load("include_online: false"), false}, {demo, true}
       }) {
    auto catalog = CreateNode("ha_catalog", config);
    auto scene = CreateNode("ha_scene", demo_scene);
    NodePush(catalog, "mcp_catalog_snapshot", Snapshot());
    NodePush(scene, "ha_catalog", NodeGet<ha::CatalogPtr>(catalog, "ha_catalog"));
    const auto text = NodeGet<SceneSnapshot>(scene, "scene_snapshot").text;
    CHECK(
      (text.find(
         "<d_light>0 Bedroom lamp "
         "[Bedroom]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off><p_ro_online>\n"
       )
       != std::string::npos)
      == online
    );
    CHECK(text.find("<d_light>0 Bedroom lamp [Bedroom]") != std::string::npos);
  }
}
