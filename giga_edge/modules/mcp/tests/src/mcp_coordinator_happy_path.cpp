#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeMcpService;

TEST_CASE("MCP coordinator handles mixed happy-path pipeline across consumers", "[mcp][coordinator][happy_path]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "alpha", .tool_name = "search"},
    {.server_id = "beta", .tool_name = "calendar"},
  };
  service->resources_snapshot = {
    {.server_id = "alpha", .uri = "resource://clock", .name = "Clock"},
    {.server_id = "beta", .uri = "resource://tasks", .name = "Tasks"},
  };
  service->resource_templates_snapshot = {
    {.server_id = "alpha", .uri_template = "resource://clock/{id}", .name = "ClockT"},
    {.server_id = "beta", .uri_template = "resource://tasks/{id}", .name = "TasksT"},
  };

  auto coordinator = CreateMcpCoordinator(service);
  const auto all_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "all",
    }
  );
  const auto alpha_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(all_consumer, "alpha", "resource://clock"));
  REQUIRE(coordinator->SubscribeResource(all_consumer, "beta", "resource://tasks"));
  REQUIRE(coordinator->SubscribeResource(alpha_consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 2);

  const auto all_tool = coordinator->SubmitToolCall(
    all_consumer,
    McpToolCallRequest{
      .call_id = "tool-all",
      .server_id = "beta",
      .tool_name = "calendar",
    }
  );
  const auto alpha_tool = coordinator->SubmitToolCall(
    alpha_consumer,
    McpToolCallRequest{
      .call_id = "tool-alpha",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto all_read = coordinator->SubmitReadResource(
    all_consumer,
    McpReadResourceRequest{
      .call_id = "read-all",
      .server_id = "beta",
      .uri = "resource://tasks",
    }
  );
  const auto alpha_ping = coordinator->SubmitPing(
    alpha_consumer,
    McpPingRequest{
      .call_id = "ping-alpha",
      .server_id = "alpha",
    }
  );

  REQUIRE(all_tool.accepted);
  REQUIRE(alpha_tool.accepted);
  REQUIRE(all_read.accepted);
  REQUIRE(alpha_ping.accepted);

  service->EmitToolCompletion(
    alpha_tool.token,
    McpToolCallResultEvent{
      .call_id = "tool-alpha",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  service->EmitPingCompletion(
    alpha_ping.token,
    McpPingResultEvent{
      .call_id = "ping-alpha",
      .server_id = "alpha",
    }
  );
  service->EmitReadCompletion(
    all_read.token,
    McpReadResourceResultEvent{
      .call_id = "read-all",
      .server_id = "beta",
      .uri = "resource://tasks",
      .mime_type = "text/plain",
      .text = "todo",
    }
  );
  service->EmitToolCompletion(
    all_tool.token,
    McpToolCallResultEvent{
      .call_id = "tool-all",
      .server_id = "beta",
      .tool_name = "calendar",
    }
  );

  Json::Value clock(Json::objectValue);
  clock["value"] = "10:00";
  Json::Value tasks(Json::objectValue);
  tasks["value"] = "3";
  service->EmitResourceUpdate("alpha", "resource://clock", clock);
  service->EmitResourceUpdate("beta", "resource://tasks", tasks);

  const auto all_tool_results = coordinator->TakeToolCallResults(all_consumer, 10);
  const auto all_read_results = coordinator->TakeReadResourceResults(all_consumer, 10);
  const auto all_ping_results = coordinator->TakePingResults(all_consumer, 10);
  const auto alpha_tool_results = coordinator->TakeToolCallResults(alpha_consumer, 10);
  const auto alpha_read_results = coordinator->TakeReadResourceResults(alpha_consumer, 10);
  const auto alpha_ping_results = coordinator->TakePingResults(alpha_consumer, 10);

  REQUIRE(all_tool_results.items.size() == 1);
  REQUIRE(all_tool_results.items[0].token == all_tool.token);
  REQUIRE(all_read_results.items.size() == 1);
  REQUIRE(all_read_results.items[0].token == all_read.token);
  REQUIRE(all_ping_results.items.empty());

  REQUIRE(alpha_tool_results.items.size() == 1);
  REQUIRE(alpha_tool_results.items[0].token == alpha_tool.token);
  REQUIRE(alpha_read_results.items.empty());
  REQUIRE(alpha_ping_results.items.size() == 1);
  REQUIRE(alpha_ping_results.items[0].token == alpha_ping.token);

  const auto all_latest = coordinator->GetLatestResourceStatesSnapshot(all_consumer);
  REQUIRE(all_latest.data);
  REQUIRE(all_latest.data->size() == 2);

  const auto alpha_latest = coordinator->GetLatestResourceStatesSnapshot(alpha_consumer);
  REQUIRE(alpha_latest.data);
  REQUIRE(alpha_latest.data->size() == 1);
  REQUIRE(alpha_latest.data->at(0).server_id == "alpha");
  REQUIRE(alpha_latest.data->at(0).uri == "resource://clock");
  REQUIRE(alpha_latest.data->at(0).data["data"]["value"].asString() == "10:00");
}

TEST_CASE(
  "MCP coordinator keeps request and subscription pipeline stable during interleaved events",
  "[mcp][coordinator][happy_path]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "pipeline",
      .visible_server_ids = {"alpha"},
      .completion_queue_capacity = 8,
    }
  );

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  const auto first_tool = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "tool-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto second_tool = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "tool-2",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto read = coordinator->SubmitReadResource(
    consumer,
    McpReadResourceRequest{
      .call_id = "read-1",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  );
  REQUIRE(first_tool.accepted);
  REQUIRE(second_tool.accepted);
  REQUIRE(read.accepted);

  Json::Value first_update(Json::objectValue);
  first_update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", first_update);

  service->EmitToolCompletion(
    second_tool.token,
    McpToolCallResultEvent{
      .call_id = "tool-2",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  service->EmitReadCompletion(
    read.token,
    McpReadResourceResultEvent{
      .call_id = "read-1",
      .server_id = "alpha",
      .uri = "resource://clock",
      .mime_type = "text/plain",
      .text = "10:00",
    }
  );
  service->EmitToolCompletion(
    first_tool.token,
    McpToolCallResultEvent{
      .call_id = "tool-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );

  Json::Value second_update(Json::objectValue);
  second_update["value"] = "10:01";
  service->EmitResourceUpdate("alpha", "resource://clock", second_update);

  const auto tool_results = coordinator->TakeToolCallResults(consumer, 10);
  REQUIRE(tool_results.items.size() == 2);
  REQUIRE(tool_results.items[0].token == second_tool.token);
  REQUIRE(tool_results.items[1].token == first_tool.token);

  const auto read_results = coordinator->TakeReadResourceResults(consumer, 10);
  REQUIRE(read_results.items.size() == 1);
  REQUIRE(read_results.items[0].token == read.token);
  REQUIRE(read_results.items[0].result.text == "10:00");

  const auto latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(latest.data);
  REQUIRE(latest.data->size() == 1);
  REQUIRE(latest.data->at(0).data["data"]["value"].asString() == "10:01");
}

TEST_CASE("MCP registered consumers remain discoverable by name", "[mcp][coordinator]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);
  McpConsumerConfig config;
  config.consumer_name = "named-consumer";
  const auto registered = coordinator->RegisterConsumer(config);
  REQUIRE(coordinator->EnsureConsumer(config) == registered);
  coordinator->UnregisterConsumer(registered);
  const auto replacement = coordinator->EnsureConsumer(config);
  REQUIRE(replacement != registered);
  REQUIRE(coordinator->EnsureConsumer(config) == replacement);
}
