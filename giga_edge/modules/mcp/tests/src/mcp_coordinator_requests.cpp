#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeMcpService;

TEST_CASE("MCP coordinator routes completions to owning consumer", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto first_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "first",
    }
  );
  const auto second_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "second",
    }
  );

  const auto tool = coordinator->SubmitToolCall(
    first_consumer,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto read = coordinator->SubmitReadResource(
    first_consumer,
    McpReadResourceRequest{
      .call_id = "read-call",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  );
  const auto ping = coordinator->SubmitPing(
    first_consumer,
    McpPingRequest{
      .call_id = "ping-call",
      .server_id = "alpha",
    }
  );

  REQUIRE(tool.accepted);
  REQUIRE(read.accepted);
  REQUIRE(ping.accepted);

  service->EmitToolCompletion(
    tool.token,
    McpToolCallResultEvent{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  service->EmitReadCompletion(
    read.token,
    McpReadResourceResultEvent{
      .call_id = "read-call",
      .server_id = "alpha",
      .uri = "resource://clock",
      .mime_type = "text/plain",
      .text = "10:00",
    }
  );
  service->EmitPingCompletion(
    ping.token,
    McpPingResultEvent{
      .call_id = "ping-call",
      .server_id = "alpha",
    }
  );

  auto first_tool_results = coordinator->TakeToolCallResults(first_consumer, 10);
  auto first_read_results = coordinator->TakeReadResourceResults(first_consumer, 10);
  auto first_ping_results = coordinator->TakePingResults(first_consumer, 10);
  auto second_tool_results = coordinator->TakeToolCallResults(second_consumer, 10);
  auto second_read_results = coordinator->TakeReadResourceResults(second_consumer, 10);
  auto second_ping_results = coordinator->TakePingResults(second_consumer, 10);

  REQUIRE(first_tool_results.items.size() == 1);
  REQUIRE(first_read_results.items.size() == 1);
  REQUIRE(first_ping_results.items.size() == 1);
  REQUIRE(first_tool_results.items[0].token == tool.token);
  REQUIRE(first_read_results.items[0].token == read.token);
  REQUIRE(first_ping_results.items[0].token == ping.token);
  REQUIRE(second_tool_results.items.empty());
  REQUIRE(second_read_results.items.empty());
  REQUIRE(second_ping_results.items.empty());
}

TEST_CASE("MCP coordinator allows cancel only for request owner", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto owner = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "owner",
    }
  );
  const auto stranger = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "stranger",
    }
  );

  const auto submit = coordinator->SubmitToolCall(
    owner,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  REQUIRE(submit.accepted);

  REQUIRE_FALSE(coordinator->CancelRequest(stranger, submit.token));
  REQUIRE(service->cancelled_tokens.empty());

  REQUIRE(coordinator->CancelRequest(owner, submit.token));
  REQUIRE(service->cancelled_tokens.size() == 1);
  REQUIRE(service->cancelled_tokens[0] == submit.token);
}

TEST_CASE("MCP coordinator rejects requests to invisible servers", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );

  const auto tool = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "beta",
      .tool_name = "search",
    }
  );
  const auto read = coordinator->SubmitReadResource(
    consumer,
    McpReadResourceRequest{
      .call_id = "read-call",
      .server_id = "beta",
      .uri = "resource://clock",
    }
  );
  const auto ping = coordinator->SubmitPing(
    consumer,
    McpPingRequest{
      .call_id = "ping-call",
      .server_id = "beta",
    }
  );

  REQUIRE_FALSE(tool.accepted);
  REQUIRE_FALSE(read.accepted);
  REQUIRE_FALSE(ping.accepted);
  REQUIRE(service->submitted_tool_tokens.empty());
  REQUIRE(service->submitted_read_tokens.empty());
  REQUIRE(service->submitted_ping_tokens.empty());
}

TEST_CASE("MCP coordinator rolls back pending request after submit rejection", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  service->accept_requests = false;
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "rollback_consumer",
    }
  );

  const auto submit = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );

  REQUIRE_FALSE(submit.accepted);
  REQUIRE(service->submitted_tool_tokens.size() == 1);
  REQUIRE_FALSE(coordinator->CancelRequest(consumer, service->submitted_tool_tokens[0]));

  service->EmitToolCompletion(
    service->submitted_tool_tokens[0],
    McpToolCallResultEvent{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );

  const auto batch = coordinator->TakeToolCallResults(consumer, 10);
  REQUIRE(batch.items.empty());
}

TEST_CASE("MCP coordinator drops oldest completion on overflow", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "overflow_consumer",
      .completion_queue_capacity = 2,
    }
  );

  const auto first = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "tool-1",
    }
  );
  const auto second = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "call-2",
      .server_id = "alpha",
      .tool_name = "tool-2",
    }
  );
  const auto third = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "call-3",
      .server_id = "alpha",
      .tool_name = "tool-3",
    }
  );

  REQUIRE(first.accepted);
  REQUIRE(second.accepted);
  REQUIRE(third.accepted);

  service->EmitToolCompletion(
    first.token,
    McpToolCallResultEvent{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "tool-1",
    }
  );
  service->EmitToolCompletion(
    second.token,
    McpToolCallResultEvent{
      .call_id = "call-2",
      .server_id = "alpha",
      .tool_name = "tool-2",
    }
  );
  service->EmitToolCompletion(
    third.token,
    McpToolCallResultEvent{
      .call_id = "call-3",
      .server_id = "alpha",
      .tool_name = "tool-3",
    }
  );

  const auto batch = coordinator->TakeToolCallResults(consumer, 10);
  REQUIRE(batch.overflow_happened);
  REQUIRE(batch.items.size() == 2);
  REQUIRE(batch.items[0].token == second.token);
  REQUIRE(batch.items[1].token == third.token);
}

TEST_CASE("MCP coordinator unregister cancels all owned pending requests", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto first_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "first",
    }
  );
  const auto second_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "second",
    }
  );

  const auto owned_tool = coordinator->SubmitToolCall(
    first_consumer,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto owned_read = coordinator->SubmitReadResource(
    first_consumer,
    McpReadResourceRequest{
      .call_id = "read-call",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  );
  const auto other_ping = coordinator->SubmitPing(
    second_consumer,
    McpPingRequest{
      .call_id = "ping-call",
      .server_id = "alpha",
    }
  );

  REQUIRE(owned_tool.accepted);
  REQUIRE(owned_read.accepted);
  REQUIRE(other_ping.accepted);

  coordinator->UnregisterConsumer(first_consumer);

  REQUIRE(service->cancelled_tokens.size() == 2);
  REQUIRE((service->cancelled_tokens[0] == owned_tool.token || service->cancelled_tokens[1] == owned_tool.token));
  REQUIRE((service->cancelled_tokens[0] == owned_read.token || service->cancelled_tokens[1] == owned_read.token));
  REQUIRE_FALSE(coordinator->CancelRequest(second_consumer, owned_tool.token));
}

TEST_CASE("MCP coordinator rejects invalid request payloads before touching service", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);
  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "invalid_payloads",
    }
  );

  const auto invalid_tool = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto invalid_read = coordinator->SubmitReadResource(
    consumer,
    McpReadResourceRequest{
      .call_id = "read-1",
      .server_id = "alpha",
      .uri = "",
    }
  );
  const auto invalid_ping = coordinator->SubmitPing(
    consumer,
    McpPingRequest{
      .call_id = "",
      .server_id = "alpha",
    }
  );

  REQUIRE_FALSE(invalid_tool.accepted);
  REQUIRE(invalid_tool.error_message == "Invalid tool call request");
  REQUIRE_FALSE(invalid_read.accepted);
  REQUIRE(invalid_read.error_message == "Invalid resource read request");
  REQUIRE_FALSE(invalid_ping.accepted);
  REQUIRE(invalid_ping.error_message == "Invalid ping request");
  REQUIRE(service->submitted_tool_tokens.empty());
  REQUIRE(service->submitted_read_tokens.empty());
  REQUIRE(service->submitted_ping_tokens.empty());
}

TEST_CASE("MCP coordinator returns clear errors for unknown consumer requests", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto tool = coordinator->SubmitToolCall(
    9999,
    McpToolCallRequest{
      .call_id = "tool-call",
      .server_id = "alpha",
      .tool_name = "search",
    }
  );
  const auto read = coordinator->SubmitReadResource(
    9999,
    McpReadResourceRequest{
      .call_id = "read-call",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  );
  const auto ping = coordinator->SubmitPing(
    9999,
    McpPingRequest{
      .call_id = "ping-call",
      .server_id = "alpha",
    }
  );

  REQUIRE_FALSE(tool.accepted);
  REQUIRE(tool.error_message == "Unknown MCP consumer");
  REQUIRE_FALSE(read.accepted);
  REQUIRE(read.error_message == "Unknown MCP consumer");
  REQUIRE_FALSE(ping.accepted);
  REQUIRE(ping.error_message == "Unknown MCP consumer");
  REQUIRE_FALSE(coordinator->CancelRequest(9999, 1));
}

TEST_CASE("MCP coordinator reports overflow once and clears flag after take", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "overflow_once",
      .completion_queue_capacity = 1,
    }
  );

  const auto first = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "tool-1",
    }
  );
  const auto second = coordinator->SubmitToolCall(
    consumer,
    McpToolCallRequest{
      .call_id = "call-2",
      .server_id = "alpha",
      .tool_name = "tool-2",
    }
  );
  REQUIRE(first.accepted);
  REQUIRE(second.accepted);

  service->EmitToolCompletion(
    first.token,
    McpToolCallResultEvent{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "tool-1",
    }
  );
  service->EmitToolCompletion(
    second.token,
    McpToolCallResultEvent{
      .call_id = "call-2",
      .server_id = "alpha",
      .tool_name = "tool-2",
    }
  );

  const auto first_take = coordinator->TakeToolCallResults(consumer, 10);
  REQUIRE(first_take.overflow_happened);
  REQUIRE(first_take.items.size() == 1);
  REQUIRE(first_take.items[0].token == second.token);

  const auto second_take = coordinator->TakeToolCallResults(consumer, 10);
  REQUIRE_FALSE(second_take.overflow_happened);
  REQUIRE(second_take.items.empty());
}

TEST_CASE("MCP coordinator EnsureConsumer returns the same id for the same name", "[mcp][coordinator][requests]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto first = coordinator->EnsureConsumer(
    McpConsumerConfig{
      .consumer_name = "shared_pipeline",
    }
  );
  const auto second = coordinator->EnsureConsumer(
    McpConsumerConfig{
      .consumer_name = "shared_pipeline",
    }
  );
  const auto other = coordinator->EnsureConsumer(
    McpConsumerConfig{
      .consumer_name = "other_pipeline",
    }
  );

  REQUIRE(first != 0);
  REQUIRE(second == first);
  REQUIRE(other != first);
}
