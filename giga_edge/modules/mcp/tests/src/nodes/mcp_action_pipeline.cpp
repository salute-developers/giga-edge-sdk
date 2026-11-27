#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_executor/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_resolver/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_tool_result/node.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>
#include <tuple>

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

McpCatalogSnapshot HomeCatalog() {
  McpCatalogSnapshot snapshot;
  snapshot.revision = "test";
  snapshot.tools.push_back(
    McpCatalogTool{
      .server_id = "home",
      .tool_name = "set_volume",
      .description = "Set volume",
      .input_schema_json = R"({"type":"object"})",
    }
  );
  snapshot.resources.push_back(
    McpCatalogResource{
      .server_id = "home",
      .uri = "resource://speaker/state",
      .name = "Speaker state",
      .mime_type = "application/json",
      .description = "Current state",
      .subscribable = true,
    }
  );
  snapshot.resources.push_back(
    McpCatalogResource{
      .server_id = "home",
      .uri = "resource://speaker/static",
      .name = "Speaker static data",
      .mime_type = "application/json",
      .description = "Static data",
      .subscribable = false,
    }
  );
  return snapshot;
}

AssistantResult ToolCallResult() {
  AssistantResult result;
  result.is_final = true;
  result.tool_calls.push_back(
    ToolCall{
      .id = "request/tool/1",
      .name = "call_tool",
      .arguments_json = R"({"server_id":"home","tool_name":"set_volume","arguments":{"level":7}})"
    }
  );
  return result;
}
} // namespace

TEST_CASE("mcp_action_resolver resolves generic MCP tool calls", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  AssistantResult assistant_result;
  assistant_result.is_final = true;
  assistant_result.status = AssistantStatus::OK;
  assistant_result.tool_calls.push_back(
    ToolCall{
      .name = "call_tool",
      .arguments_json = R"({"server_id":"home","tool_name":"set_volume","arguments_json":"{\"level\":42}"})"
    }
  );

  NodePush(resolver, "assistant_result", assistant_result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());

  const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  REQUIRE(action.status == AssistantStatus::OK);
  REQUIRE(action.kind == McpResolvedActionKind::CALL_TOOL);
  REQUIRE(action.server_id == "home");
  REQUIRE(action.tool_name == "set_volume");
  REQUIRE(action.arguments_json == R"({"level":42})");
}

TEST_CASE("mcp_action_resolver keeps plain assistant text skipped for MCP", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  AssistantResult assistant_result;
  assistant_result.text = "Привет, чем помочь?";
  assistant_result.is_final = true;
  assistant_result.status = AssistantStatus::OK;

  NodePush(resolver, "assistant_result", assistant_result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());

  const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  REQUIRE(action.status == AssistantStatus::SKIPPED);
  REQUIRE(action.kind == McpResolvedActionKind::NONE);
  REQUIRE(action.assistant_text == "Привет, чем помочь?");
}

TEST_CASE("mcp_action_resolver never submits calls from a truncated generation", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  auto result = ToolCallResult();
  result.finish_reason = StreamFinishReason::MAX_TOKENS;
  NodePush(resolver, "assistant_result", result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());
  const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  REQUIRE(action.kind == McpResolvedActionKind::NONE);
  REQUIRE(action.status == AssistantStatus::SKIPPED);
}

TEST_CASE("mcp_action_resolver validates resources and subscriptions", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  AssistantResult read_result;
  read_result.is_final = true;
  read_result.status = AssistantStatus::OK;
  read_result.tool_calls.push_back(
    ToolCall{.name = "read_resource", .arguments_json = R"({"server_id":"home","uri":"resource://speaker/state"})"}
  );

  NodePush(resolver, "assistant_result", read_result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());
  {
    const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
    REQUIRE(action.status == AssistantStatus::OK);
    REQUIRE(action.kind == McpResolvedActionKind::READ_RESOURCE);
    REQUIRE(action.uri == "resource://speaker/state");
  }

  AssistantResult subscribe_result;
  subscribe_result.is_final = true;
  subscribe_result.status = AssistantStatus::OK;
  subscribe_result.tool_calls.push_back(
    ToolCall{
      .name = "subscribe_resource", .arguments_json = R"({"server_id":"home","uri":"resource://speaker/static"})"
    }
  );

  NodePush(resolver, "assistant_result", subscribe_result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());
  {
    const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
    REQUIRE(action.status == AssistantStatus::ERROR);
    REQUIRE(action.error.find("not subscribable") != std::string::npos);
  }
}

TEST_CASE("mcp_action_resolver routes a server tool named like an envelope action to the server", "[mcp][nodes]") {
  const auto [name, kind, arguments] = GENERATE(
    std::make_tuple(
      std::string("call_tool"),
      McpResolvedActionKind::CALL_TOOL,
      std::string(R"({"server_id":"home","tool_name":"set_volume","arguments":{"level":7}})")
    ),
    std::make_tuple(
      std::string("read_resource"),
      McpResolvedActionKind::READ_RESOURCE,
      std::string(R"({"server_id":"home","uri":"resource://speaker/state"})")
    ),
    std::make_tuple(
      std::string("subscribe_resource"),
      McpResolvedActionKind::SUBSCRIBE_RESOURCE,
      std::string(R"({"server_id":"home","uri":"resource://speaker/state"})")
    ),
    std::make_tuple(
      std::string("unsubscribe_resource"),
      McpResolvedActionKind::UNSUBSCRIBE_RESOURCE,
      std::string(R"({"server_id":"home","uri":"resource://speaker/state"})")
    )
  );
  const bool direct_actions = GENERATE(false, true);
  CAPTURE(name, direct_actions);
  auto resolver = CreateNode(
    "mcp_action_resolver",
    std::string("profile: {allow_direct_tool_actions: ") + (direct_actions ? "true" : "false") + "}"
  );
  auto catalog = HomeCatalog();
  catalog.tools.push_back(
    McpCatalogTool{.server_id = "home", .tool_name = name, .input_schema_json = R"({"type":"object"})"}
  );
  AssistantResult assistant_result;
  assistant_result.is_final = true;
  assistant_result.status = AssistantStatus::OK;
  assistant_result.tool_calls.push_back(ToolCall{.name = name, .arguments_json = arguments});

  NodePush(resolver, "assistant_result", assistant_result);
  NodePush(resolver, "mcp_catalog_snapshot", catalog);

  const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  REQUIRE(action.status == AssistantStatus::OK);
  REQUIRE(action.server_id == "home");
  if (direct_actions) {
    REQUIRE(action.kind == McpResolvedActionKind::CALL_TOOL);
    REQUIRE(action.tool_name == name);
    Json::Value forwarded;
    Json::Value expected;
    Json::Reader reader;
    REQUIRE(reader.parse(action.arguments_json, forwarded));
    REQUIRE(reader.parse(arguments, expected));
    REQUIRE(forwarded == expected);
  } else {
    // Without direct calls the name stays an envelope action.
    REQUIRE(action.kind == kind);
  }
}

TEST_CASE("mcp_action_resolver stays silent between calls even with a broken catalog", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  McpCatalogSnapshot broken;
  broken.status = AssistantStatus::ERROR;
  broken.error = "catalog unavailable";
  auto tick = [&](const AssistantResult& result) {
    NodePush(resolver, "assistant_result", result);
    NodePush(resolver, "mcp_catalog_snapshot", broken);
    return NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  };

  REQUIRE(tick(AssistantResult{}).status == AssistantStatus::SKIPPED);
  AssistantResult failed;
  failed.is_final = true;
  failed.status = AssistantStatus::ERROR;
  failed.error = "Malformed tool call";
  REQUIRE(tick(failed).status == AssistantStatus::SKIPPED);

  const auto action = tick(ToolCallResult());
  REQUIRE(action.status == AssistantStatus::ERROR);
  REQUIRE(action.error == "catalog unavailable");
  REQUIRE(action.call_id == "request/tool/1");
  REQUIRE(action.source_action_name == "call_tool");
}

TEST_CASE("mcp_action_resolver answers several calls with an error for the first one", "[mcp][nodes]") {
  auto resolver = CreateNode("mcp_action_resolver", "");
  auto result = ToolCallResult();
  result.tool_calls.push_back(ToolCall{.id = "request/tool/2", .name = "call_tool"});
  NodePush(resolver, "assistant_result", result);
  NodePush(resolver, "mcp_catalog_snapshot", HomeCatalog());
  const auto action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
  REQUIRE(action.status == AssistantStatus::ERROR);
  REQUIRE(action.call_id == "request/tool/1");
}

TEST_CASE("mcp_action_resolver rejects an unknown profile", "[mcp][nodes]") {
  REQUIRE_THROWS(CreateNode("mcp_action_resolver", "profile: missing_v9"));
}

TEST_CASE("mcp_action_executor submits tool and resource actions asynchronously", "[mcp][nodes]") {
  auto service = std::make_shared<FakeMcpService>();
  auto environments = MakeMcpEnvironment(service);
  auto executor = CreateNode(
    "mcp_action_executor",
    R"(
      consumer_name: "executor_test"
      visible_server_ids: ["home"]
      call_id_prefix: "test-call-"
    )",
    {},
    {},
    {},
    {},
    &environments
  );

  NodePush(
    executor,
    "mcp_resolved_action",
    McpResolvedAction{
      .kind = McpResolvedActionKind::CALL_TOOL,
      .server_id = "home",
      .tool_name = "set_volume",
      .arguments_json = R"({"level":42})",
    }
  );
  {
    const auto result = NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
    REQUIRE(result.status == AssistantStatus::OK);
    REQUIRE(result.submitted);
    REQUIRE(result.action.call_id == "test-call-1");
    REQUIRE(result.request_token != 0);
    REQUIRE(result.action.kind == McpResolvedActionKind::CALL_TOOL);
    REQUIRE(service->submitted_tool_tokens.size() == 1);
    REQUIRE(service->last_tool_request.call_id == "test-call-1");
    REQUIRE(service->last_tool_request.server_id == "home");
    REQUIRE(service->last_tool_request.tool_name == "set_volume");
    REQUIRE(service->last_tool_request.arguments["level"].asInt() == 42);
  }

  NodePush(
    executor,
    "mcp_resolved_action",
    McpResolvedAction{
      .kind = McpResolvedActionKind::READ_RESOURCE,
      .server_id = "home",
      .uri = "resource://speaker/state",
    }
  );
  {
    const auto result = NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
    REQUIRE(result.status == AssistantStatus::OK);
    REQUIRE(result.submitted);
    REQUIRE(result.action.call_id == "test-call-2");
    REQUIRE(result.action.kind == McpResolvedActionKind::READ_RESOURCE);
    REQUIRE(service->submitted_read_tokens.size() == 1);
    REQUIRE(service->last_read_request.uri == "resource://speaker/state");
  }
}

TEST_CASE("mcp_action_executor submits subscribe and exposes state to catalog snapshot", "[mcp][nodes][pipeline]") {
  auto service = std::make_shared<FakeMcpService>();
  service->auto_complete_subscriptions = false;
  service->resources_snapshot = {
    {.server_id = "home", .uri = "resource://speaker/state", .name = "Speaker state", .subscribable = true},
  };
  auto environments = MakeMcpEnvironment(service);
  auto executor = CreateNode(
    "mcp_action_executor",
    R"(
      consumer_name: "executor_test"
      visible_server_ids: ["home"]
    )",
    {},
    {},
    {},
    {},
    &environments
  );
  auto snapshot_node = CreateNode(
    "mcp_catalog_snapshot",
    R"(
      consumer_name: "executor_test_scene"
      visible_server_ids: ["home"]
      include_shared_latest_resource_states: true
    )",
    {},
    {},
    {},
    {},
    &environments
  );

  NodePush(
    executor,
    "mcp_resolved_action",
    McpResolvedAction{
      .kind = McpResolvedActionKind::SUBSCRIBE_RESOURCE,
      .server_id = "home",
      .uri = "resource://speaker/state",
    }
  );
  const auto result = NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
  REQUIRE(result.status == AssistantStatus::OK);
  REQUIRE(result.submitted);
  REQUIRE(service->subscription_operation_calls.size() == 1);
  REQUIRE(service->subscription_operation_calls[0].server_id == "home");
  service->EmitResourceSubscriptionResult(
    giga_edge::mcp::McpResourceSubscriptionEvent{
      .operation_id = service->subscription_operation_calls[0].operation_id,
      .server_id = "home",
      .uri = "resource://speaker/state",
      .operation = giga_edge::mcp::McpResourceSubscriptionEvent::Operation::SUBSCRIBE,
    }
  );

  service->EmitResourceUpdate("home", "resource://speaker/state", [] {
    Json::Value data(Json::objectValue);
    data["volume"] = 42;
    return data;
  }());

  NodePush(snapshot_node, "user_query", UserQuery{.request_id = "r1", .text = "state"});
  const auto snapshot = NodeGet<McpCatalogSnapshot>(snapshot_node, "mcp_catalog_snapshot");
  REQUIRE(snapshot.latest_resource_states.size() == 1);
  REQUIRE(snapshot.latest_resource_states[0].server_id == "home");
  REQUIRE(snapshot.latest_resource_states[0].uri == "resource://speaker/state");
  REQUIRE(snapshot.latest_resource_states[0].data_json.find(R"("volume":42)") != std::string::npos);
}

TEST_CASE("resolver executor pipeline submits configured MCP tool call", "[mcp][pipeline]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "home", .tool_name = "set_volume"},
  };
  auto environments = MakeMcpEnvironment(service);

  auto catalog = CreateNode("mcp_catalog_snapshot", R"(visible_server_ids: ["home"])", {}, {}, {}, {}, &environments);
  auto resolver = CreateNode("mcp_action_resolver", "");
  auto executor = CreateNode("mcp_action_executor", R"(visible_server_ids: ["home"])", {}, {}, {}, {}, &environments);

  NodePush(catalog, "user_query", UserQuery{.request_id = "r1", .text = "set volume"});
  const auto parsed = ToolCallResult();

  NodePush(resolver, "assistant_result", parsed);
  NodePush(resolver, "mcp_catalog_snapshot", NodeGet<McpCatalogSnapshot>(catalog, "mcp_catalog_snapshot"));
  NodePush(executor, "mcp_resolved_action", NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action"));

  const auto result = NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
  REQUIRE(result.status == AssistantStatus::OK);
  REQUIRE(result.submitted);
  REQUIRE(service->submitted_tool_tokens.size() == 1);
  REQUIRE(service->last_tool_request.arguments["level"].asInt() == 7);
}

namespace {

struct ResultPath {
  NodeContext pickup;
  NodeContext format;

  ResultPath(EnvironmentsMap& environments, const std::string& consumer) {
    pickup = CreateNode(
      "mcp_completion_pickup",
      "consumer_name: " + consumer + "\nowns_consumer_registration: false",
      {},
      {"dialogue_reset"},
      {},
      {},
      &environments
    );
    format = CreateNode("mcp_tool_result", "");
  }

  ToolResult Tick(const McpActionSubmitReport& report = {}, UserQuery query = {}) {
    NodePush(pickup, "dialogue_reset", false, true, 0);
    NodePush(pickup, "user_query", query);
    NodePush(pickup, "submission", report);
    NodePush(format, "mcp_completions", NodeGet<McpActionCompletions>(pickup, "mcp_completions"));
    return NodeGet<ToolResult>(format, "tool_result");
  }
};

NodeContext Executor(EnvironmentsMap& environments, const std::string& consumer) {
  return CreateNode(
    "mcp_action_executor",
    "consumer_name: " + consumer + "\nvisible_server_ids: [\"home\"]",
    {},
    {},
    {},
    {},
    &environments
  );
}

McpActionSubmitReport Submit(NodeContext& executor, const McpResolvedAction& action) {
  NodePush(executor, "mcp_resolved_action", action);
  return NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
}

} // namespace

TEST_CASE(
  "resolver executor pickup pipeline returns the tool completion to the model", "[mcp][pipeline][tool_result]"
) {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {{.server_id = "home", .tool_name = "set_volume"}};
  auto environments = MakeMcpEnvironment(service);
  auto catalog = CreateNode("mcp_catalog_snapshot", R"(visible_server_ids: ["home"])", {}, {}, {}, {}, &environments);
  auto resolver = CreateNode("mcp_action_resolver", "");
  auto executor = Executor(environments, "pipeline_shared");
  ResultPath path(environments, "pipeline_shared");

  NodePush(catalog, "user_query", UserQuery{.request_id = "r1", .text = "set volume"});
  NodePush(resolver, "assistant_result", ToolCallResult());
  NodePush(resolver, "mcp_catalog_snapshot", NodeGet<McpCatalogSnapshot>(catalog, "mcp_catalog_snapshot"));
  const auto submit = Submit(executor, NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action"));
  REQUIRE(submit.submitted);
  REQUIRE(submit.action.call_id == "request/tool/1");
  REQUIRE_FALSE(path.Tick(submit).is_ready);

  mcp::McpToolCallResultEvent completion;
  completion.call_id = submit.action.call_id;
  completion.server_id = "home";
  completion.tool_name = "set_volume";
  completion.content["status"] = "ok";
  completion.content["level"] = 7;
  service->EmitToolCompletion(service->submitted_tool_tokens.front(), completion);

  const auto result = path.Tick();
  REQUIRE(result.is_ready);
  REQUIRE(result.call_id == "request/tool/1");
  REQUIRE(result.name == "call_tool");
  REQUIRE(result.content == R"({"level":7,"status":"ok"})");
  REQUIRE_FALSE(path.Tick().is_ready);
}

TEST_CASE("executor pickup pipeline returns resource reads and tool errors", "[mcp][nodes][pipeline][tool_result]") {
  auto service = std::make_shared<FakeMcpService>();
  auto environments = MakeMcpEnvironment(service);
  auto executor = Executor(environments, "read_pipeline");
  ResultPath path(environments, "read_pipeline");

  const auto read = Submit(
    executor,
    McpResolvedAction{
      .kind = McpResolvedActionKind::READ_RESOURCE, .server_id = "home", .uri = "resource://speaker/state"
    }
  );
  REQUIRE_FALSE(path.Tick(read).is_ready);
  mcp::McpReadResourceResultEvent state;
  state.call_id = read.action.call_id;
  state.server_id = "home";
  state.uri = "resource://speaker/state";
  state.text = R"({"volume":42,"muted":false})";
  service->EmitReadCompletion(service->submitted_read_tokens.front(), state);
  auto result = path.Tick();
  REQUIRE(result.name == "resource://speaker/state");
  REQUIRE(result.content == R"({"muted":false,"volume":42})");

  const auto call = Submit(
    executor,
    McpResolvedAction{.kind = McpResolvedActionKind::CALL_TOOL, .server_id = "home", .tool_name = "set_volume"}
  );
  REQUIRE_FALSE(path.Tick(call).is_ready);
  mcp::McpToolCallResultEvent failure;
  failure.call_id = call.action.call_id;
  failure.is_error = true;
  failure.error_message = "DEVICE_UNREACHABLE";
  service->EmitToolCompletion(service->submitted_tool_tokens.front(), failure);
  result = path.Tick();
  REQUIRE(result.call_id == call.action.call_id);
  REQUIRE(result.content == R"({"error":"DEVICE_UNREACHABLE","status":"fail"})");
}

TEST_CASE("resolver errors reach the model as failed tool results", "[mcp][nodes][pipeline][tool_result]") {
  auto service = std::make_shared<FakeMcpService>();
  auto environments = MakeMcpEnvironment(service);
  auto resolver = CreateNode("mcp_action_resolver", "");
  auto executor = Executor(environments, "reject_pipeline");
  ResultPath path(environments, "reject_pipeline");

  NodePush(resolver, "assistant_result", ToolCallResult());
  NodePush(resolver, "mcp_catalog_snapshot", McpCatalogSnapshot{});
  const auto report = Submit(executor, NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action"));
  REQUIRE(report.status == AssistantStatus::ERROR);
  REQUIRE(service->submitted_tool_tokens.empty());
  const auto result = path.Tick(report);
  REQUIRE(result.is_ready);
  REQUIRE(result.call_id == "request/tool/1");
  REQUIRE(result.content.find(R"("status":"fail")") != std::string::npos);
}

TEST_CASE("a new query drops completions of the previous turn", "[mcp][nodes][pipeline][tool_result]") {
  auto service = std::make_shared<FakeMcpService>();
  auto environments = MakeMcpEnvironment(service);
  auto executor = Executor(environments, "stale_pipeline");
  ResultPath path(environments, "stale_pipeline");

  const auto call = Submit(
    executor,
    McpResolvedAction{.kind = McpResolvedActionKind::CALL_TOOL, .server_id = "home", .tool_name = "set_volume"}
  );
  REQUIRE_FALSE(path.Tick(call).is_ready);
  REQUIRE_FALSE(path.Tick({}, UserQuery{.request_id = "r2", .text = "another"}).is_ready);
  mcp::McpToolCallResultEvent late;
  late.call_id = call.action.call_id;
  service->EmitToolCompletion(service->submitted_tool_tokens.front(), late);
  for (int i = 0; i < 4; ++i) {
    REQUIRE_FALSE(path.Tick().is_ready);
  }
  REQUIRE(service->cancelled_tokens.size() == 1);
}
