#include "test_config.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_executor/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/node.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"
#include "giga_edge/modules/smart_home_ha/nodes.h"

#include <catch.hpp>
using namespace giga_edge;
using namespace giga_edge::ha_test;

TEST_CASE("HA relative, copy and online commands complete through the runtime MCP executor", "[ha][integration]") {
  const int mode = GENERATE(0, 1, 2);
  const bool copy = mode == 1;
  const bool online = mode == 2;
  const auto house = copy ? CopySnapshot() : Snapshot();
  mcp::McpServiceConfig config;
  config.resource_hydration.read_resources_on_list = true;
  config.resource_hydration.preferred_read_resource_match_phrases = {"homeassistant://assist/context-snapshot"};
  auto env = std::make_shared<mcp::McpServiceEnvironment>(config);
  EnvironmentsMap environments{{"mcp", env}};
  auto connection = std::make_shared<mcp::test_utils::FakeConnection>();
  connection->EnqueueSuccess("initialize", ParseJsonOrThrow(R"({"protocolVersion":"2025-03-26",
    "serverInfo":{"name":"ha","version":"test"},"capabilities":{"tools":{},"resources":{}}})"));
  Json::Value tools, resources, content;
  for (const auto& t : house.tools) {
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
  resource["text"] = ParseJsonOrThrow(house.latest_resource_states[0].data_json)["text"];
  content["contents"].append(resource);
  connection->EnqueueSuccess("resources/read", content);
  auto snapshot = CreateNode(
    "mcp_catalog_snapshot", "include_shared_latest_resource_states: true", {}, {}, {}, {}, &environments
  );
  auto executor = CreateNode("mcp_action_executor", "consumer_name: ha_test", {}, {}, {}, {}, &environments);
  auto pickup = CreateNode("mcp_completion_pickup", "consumer_name: ha_test", {}, {}, {}, {}, &environments);
  auto normalize = CreateNode("ha_tool_result", "");
  env->service->AddServer("ha", connection);
  ha::CatalogBuilder builder;
  ha::CatalogPtr catalog;
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    NodePush(snapshot, "user_query", UserQuery{});
    catalog = builder.Build(NodeGet<McpCatalogSnapshot>(snapshot, "mcp_catalog_snapshot"), {.include_online = online});
    return catalog->devices.count("light_0") == 1;
  }));
  auto action = ha::ResolveAction(
    online   ? Action("get_device_parameter", "online")
      : copy ? CopyAction()
             : Action("relative_change", "light_brightness", "+10%"),
    *catalog
  );
  auto unavailable = ReadContext();
  auto text = unavailable["result"].asString();
  unavailable["result"] = text.replace(text.find("state: 'on'"), 11, "state: unavailable");
  const int phases = online ? 1 : copy ? 3 : 2;
  for (int phase = 0; phase < phases; ++phase) {
    const auto reply = Reply(
      action,
      online                  ? unavailable
        : phase == phases - 1 ? Success()
        : copy && phase == 0  ? SourceContext()
                              : ReadContext()
    );
    Json::Value response;
    response["content"] = ParseJsonOrThrow(reply.content_json);
    response["isError"] = false;
    connection->EnqueueSuccess("tools/call", response);
    NodePush(executor, "mcp_resolved_action", action);
    auto report = NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
    REQUIRE(report.submitted);
    McpActionCompletions completions;
    REQUIRE(mcp::test_utils::WaitUntil([&] {
      NodePush(pickup, "user_query", UserQuery{});
      NodePush(pickup, "submission", report);
      report = {};
      completions = NodeGet<McpActionCompletions>(pickup, "mcp_completions");
      return !completions.empty();
    }));
    NodePush(normalize, "mcp_completions", completions);
    auto results = NodeGet<CompactToolResults>(normalize, "tool_results");
    auto next = NodeGet<std::vector<McpResolvedAction>>(normalize, "continuations");
    if (phase < phases - 1) {
      REQUIRE(results.empty());
      REQUIRE(next.size() == 1);
      action = next[0];
    } else {
      REQUIRE(next.empty());
      REQUIRE(results.size() == 1);
      CHECK_FALSE(results[0].is_error);
      CHECK(results[0].call_id == "query-1/tool/1");
      CHECK(results[0].value_json == (online ? "false" : ""));
    }
  }
  std::vector<Json::Value> calls;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == "tools/call") {
      calls.push_back(call.params);
    }
  }
  REQUIRE(calls.size() == static_cast<size_t>(phases));
  CHECK(calls[0]["name"] == "homeassistant__GetLiveContext");
  if (copy) {
    CHECK(calls[0]["arguments"]["name"] == "Desk lamp");
    CHECK(calls[1]["name"] == "homeassistant__GetLiveContext");
    CHECK(calls[1]["arguments"]["name"] == "Bedroom lamp");
  }
  if (!online) {
    CHECK(calls.back()["name"] == "light__HassLightSet");
    CHECK(calls.back()["arguments"]["brightness"] == (copy ? 73 : 40));
  }
  CHECK(calls.back()["arguments"]["name"] == "Bedroom lamp");
  CHECK(calls.back()["arguments"]["area"] == "Bedroom");
  env->service->RemoveServer("ha");
}
