#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_scene_renderer/node.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>

using namespace giga_edge;
using giga_edge::mcp::CreateMcpCoordinator;
using giga_edge::mcp::McpServiceConfig;
using giga_edge::mcp::McpServiceEnvironment;
using giga_edge::mcp::test_utils::FakeMcpService;

namespace {

EnvironmentsMap MakeMcpEnvironment(std::shared_ptr<FakeMcpService> service) {
  auto env = std::make_shared<McpServiceEnvironment>(McpServiceConfig{});
  env->service = service;
  env->coordinator = CreateMcpCoordinator(service);
  return EnvironmentsMap{{"mcp", env}};
}

UserQuery TickQuery() {
  return UserQuery{
    .request_id = "request-1",
    .text = "turn on the light",
  };
}

} // namespace

TEST_CASE("mcp_catalog_snapshot reads coordinator snapshots", "[mcp][nodes]") {
  auto service = std::make_shared<FakeMcpService>();
  Json::Value schema(Json::objectValue);
  schema["type"] = "object";
  service->tools_snapshot = {
    {
      .server_id = "home",
      .tool_name = "set_device_parameter",
      .description = "Set parameter",
      .input_schema = schema,
      .title = "Parameter",
    },
  };
  service->resources_snapshot = {
    {
      .server_id = "home",
      .uri = "resource://smart_home/scene",
      .name = "Scene",
      .mime_type = "application/json",
      .description = "Current scene",
      .subscribable = true,
    },
  };
  auto environments = MakeMcpEnvironment(service);

  auto node = CreateNode(
    "mcp_catalog_snapshot",
    R"(
      consumer_name: "test_scene"
      visible_server_ids: ["home"]
    )",
    {},
    {},
    {},
    {},
    &environments
  );

  NodePush(node, "user_query", TickQuery());

  const auto snapshot = NodeGet<McpCatalogSnapshot>(node, "mcp_catalog_snapshot");
  REQUIRE(snapshot.status == AssistantStatus::OK);
  REQUIRE(snapshot.tools.size() == 1);
  REQUIRE(snapshot.tools[0].server_id == "home");
  REQUIRE(snapshot.tools[0].tool_name == "set_device_parameter");
  REQUIRE(snapshot.tools[0].title == "Parameter");
  REQUIRE(snapshot.tools[0].input_schema_json.find(R"("type":"object")") != std::string::npos);
  REQUIRE(snapshot.resources.size() == 1);
  REQUIRE(snapshot.resources[0].uri == "resource://smart_home/scene");
  REQUIRE(snapshot.resources[0].subscribable);
  REQUIRE(snapshot.revision.find("tools:1") != std::string::npos);
}

TEST_CASE("mcp_catalog_snapshot filters visible servers", "[mcp][nodes]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "home", .tool_name = "set_device_parameter"},
    {.server_id = "speaker", .tool_name = "set_volume"},
  };
  auto environments = MakeMcpEnvironment(service);

  auto node = CreateNode(
    "mcp_catalog_snapshot",
    R"(
      visible_server_ids: ["speaker"]
    )",
    {},
    {},
    {},
    {},
    &environments
  );

  NodePush(node, "user_query", TickQuery());

  const auto snapshot = NodeGet<McpCatalogSnapshot>(node, "mcp_catalog_snapshot");
  REQUIRE(snapshot.tools.size() == 1);
  REQUIRE(snapshot.tools[0].server_id == "speaker");
  REQUIRE(snapshot.tools[0].tool_name == "set_volume");
}

TEST_CASE("mcp_catalog_snapshot ignores an unknown key", "[mcp][nodes]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {{.server_id = "home", .tool_name = "set_device_parameter"}};
  auto environments = MakeMcpEnvironment(service);

  auto node = CreateNode("mcp_catalog_snapshot", "unknown_key: 64", {}, {}, {}, {}, &environments);

  NodePush(node, "user_query", TickQuery());

  const auto snapshot = NodeGet<McpCatalogSnapshot>(node, "mcp_catalog_snapshot");
  REQUIRE(snapshot.status == AssistantStatus::OK);
  REQUIRE(snapshot.tools.size() == 1);
}

TEST_CASE("mcp_catalog_snapshot reports missing environment", "[mcp][nodes]") {
  EnvironmentsMap environments;
  auto node = CreateNode("mcp_catalog_snapshot", "", {}, {}, {}, {}, &environments);

  NodePush(node, "user_query", TickQuery());

  const auto snapshot = NodeGet<McpCatalogSnapshot>(node, "mcp_catalog_snapshot");
  REQUIRE(snapshot.status == AssistantStatus::ERROR);
  REQUIRE_FALSE(snapshot.error.empty());
}

TEST_CASE("mcp_catalog_snapshot output feeds mcp_scene_renderer", "[mcp][nodes][pipeline]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "home", .tool_name = "set_device_parameter", .description = "Set parameter"},
  };
  service->resources_snapshot = {
    {.server_id = "home", .uri = "resource://smart_home/scene", .name = "Scene", .subscribable = true},
  };
  auto environments = MakeMcpEnvironment(service);

  auto snapshot_node = CreateNode("mcp_catalog_snapshot", "", {}, {}, {}, {}, &environments);
  auto renderer_node = CreateNode(
    "mcp_scene_renderer",
    R"(
      allow_empty: false
    )"
  );

  NodePush(snapshot_node, "user_query", TickQuery());
  const auto snapshot = NodeGet<McpCatalogSnapshot>(snapshot_node, "mcp_catalog_snapshot");

  NodePush(renderer_node, "mcp_catalog_snapshot", snapshot);
  const auto scene = NodeGet<SceneSnapshot>(renderer_node, "scene_snapshot");

  REQUIRE(scene.status == AssistantStatus::OK);
  REQUIRE(scene.text.find("home/set_device_parameter: Set parameter") != std::string::npos);
  REQUIRE(scene.text.find("resource://smart_home/scene") != std::string::npos);
}

TEST_CASE("mcp_scene_renderer propagates upstream MCP errors", "[mcp][nodes]") {
  auto node = CreateNode("mcp_scene_renderer", "");

  McpCatalogSnapshot snapshot;
  snapshot.status = AssistantStatus::ERROR;
  snapshot.error = "no mcp environment";

  NodePush(node, "mcp_catalog_snapshot", snapshot);

  const auto scene = NodeGet<SceneSnapshot>(node, "scene_snapshot");
  REQUIRE(scene.status == AssistantStatus::ERROR);
  REQUIRE(scene.error == "no mcp environment");
}

TEST_CASE("mcp_scene_renderer supports empty scene when configured", "[mcp][nodes]") {
  auto node = CreateNode(
    "mcp_scene_renderer",
    R"(
      allow_empty: true
      include_tools: false
      include_resources: false
      include_latest_resource_states: false
    )"
  );

  McpCatalogSnapshot snapshot;
  snapshot.revision = "empty";

  NodePush(node, "mcp_catalog_snapshot", snapshot);

  const auto scene = NodeGet<SceneSnapshot>(node, "scene_snapshot");
  REQUIRE(scene.status == AssistantStatus::OK);
  REQUIRE(scene.text.empty());
  REQUIRE(scene.allow_empty);
}

TEST_CASE("mcp_scene_renderer reports unsupported format", "[mcp][nodes]") {
  auto node = CreateNode(
    "mcp_scene_renderer",
    R"(
      format: "unknown"
    )"
  );

  NodePush(node, "mcp_catalog_snapshot", McpCatalogSnapshot{});

  const auto scene = NodeGet<SceneSnapshot>(node, "scene_snapshot");
  REQUIRE(scene.status == AssistantStatus::ERROR);
  REQUIRE(scene.error.find("Unsupported MCP scene format") != std::string::npos);
}

TEST_CASE("mcp_scene_renderer plain format lists tools, resources and state", "[mcp][nodes]") {
  auto node = CreateNode("mcp_scene_renderer", "allow_empty: false\nmax_rendered_chars: 60");
  McpCatalogSnapshot snapshot;
  snapshot.tools.push_back(
    McpCatalogTool{
      .server_id = "home", .tool_name = "set_volume", .description = "Sets volume", .input_schema_json = ""
    }
  );
  snapshot.latest_resource_states.push_back(
    McpCatalogLatestResourceState{
      .server_id = "home", .uri = "resource://speaker/state", .data_json = R"({"volume":42})"
    }
  );
  NodePush(node, "mcp_catalog_snapshot", snapshot);
  const auto scene = NodeGet<SceneSnapshot>(node, "scene_snapshot");
  REQUIRE(scene.status == AssistantStatus::OK);
  REQUIRE(scene.text.rfind("Tools:\n- home/set_volume: Sets volume\nState:\n", 0) == 0);
  REQUIRE(scene.text.size() == 60);
}
