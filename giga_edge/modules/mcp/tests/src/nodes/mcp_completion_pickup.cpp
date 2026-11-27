#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_executor/node.h"
#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/node.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

using namespace giga_edge;
namespace {
struct Pipeline {
  std::shared_ptr<mcp::test_utils::FakeMcpService> service = std::make_shared<mcp::test_utils::FakeMcpService>();
  EnvironmentsMap environments;
  NodeContext executor;
  NodeContext pickup;
  explicit Pipeline(size_t capacity = 64, size_t limit = 16) {
    auto env = std::make_shared<mcp::McpServiceEnvironment>(mcp::McpServiceConfig{});
    env->service = service;
    env->coordinator = mcp::CreateMcpCoordinator(service);
    environments["mcp"] = env;
    const auto params = "consumer_name: pipeline\ncompletion_queue_capacity: " + std::to_string(capacity)
      + "\ndrain_limit: " + std::to_string(limit);
    executor = CreateNode("mcp_action_executor", params, {}, {}, {}, {}, &environments);
    pickup = CreateNode("mcp_completion_pickup", params, {}, {"dialogue_reset"}, {}, {}, &environments);
  }
  McpActionSubmitReport Submit(McpResolvedAction action) {
    NodePush(executor, "mcp_resolved_action", action);
    return NodeGet<McpActionSubmitReport>(executor, "mcp_action_result");
  }
  McpActionCompletions Poll(const McpActionSubmitReport& report = {}, UserQuery query = {}, bool reset = false) {
    NodePush(pickup, "dialogue_reset", reset, true, 0);
    NodePush(pickup, "user_query", query);
    NodePush(pickup, "submission", report);
    return NodeGet<McpActionCompletions>(pickup, "mcp_completions");
  }
  McpActionSubmitReport Call(const std::string& context) {
    return Submit(
      {.kind = McpResolvedActionKind::CALL_TOOL,
       .server_id = "home",
       .tool_name = "test",
       .arguments_json = "{}",
       .context_json = context}
    );
  }
  void Complete(const McpActionSubmitReport& report, bool error = false) {
    mcp::McpToolCallResultEvent result;
    result.call_id = report.action.call_id;
    result.server_id = report.action.server_id;
    result.tool_name = report.action.tool_name;
    result.is_error = error;
    result.error_message = error ? "DEVICE_UNREACHABLE" : "";
    result.content["opaque"] = "payload";
    result.structured_content["value"] = 42;
    service->EmitToolCompletion(report.request_token, result);
  }
};
} // namespace

TEST_CASE("MCP pickup is idle without results", "[mcp][nodes][tool_result]") {
  Pipeline p;
  REQUIRE(p.Poll().empty());
}
TEST_CASE("MCP new query or reset cancels even a just-submitted request", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const bool reset = GENERATE(false, true);
  const auto report = p.Call("old");
  REQUIRE(p.Poll(report, reset ? UserQuery{} : UserQuery{.request_id = "new", .text = "query"}, reset).empty());
  p.Complete(report);
  REQUIRE(p.Poll().empty());
  REQUIRE(p.service->cancelled_tokens.size() == 1);
}
TEST_CASE("MCP pickup preserves every result and caller context", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const auto one = p.Call("one");
  REQUIRE(one.submitted);
  REQUIRE(p.Poll(one).empty());
  const auto two = p.Call("two");
  REQUIRE(p.Poll(two).empty());
  p.Complete(two);
  p.Complete(one);
  const auto results = p.Poll();
  REQUIRE(results.size() == 2);
  REQUIRE(results[0].action.context_json == "two");
  REQUIRE(results[1].action.context_json == "one");
  REQUIRE(results[0].structured_content_json == R"({"value":42})");
  REQUIRE(results[0].content_json == R"({"opaque":"payload"})");
  REQUIRE(p.Poll().empty());
}
TEST_CASE("MCP pickup holds a reply that overtakes its submission report", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const auto report = p.Call("fast");
  p.Complete(report);
  REQUIRE(p.Poll().empty());
  const auto out = p.Poll(report);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].action.context_json == "fast");
  REQUIRE(p.Poll().empty());

  const auto lost = p.Call("lost");
  p.Complete(lost);
  for (int i = 0; i < 4; ++i) {
    REQUIRE(p.Poll().empty());
  }
  REQUIRE(p.Poll(lost).empty());
}
TEST_CASE("MCP pickup retains results beyond drain limit", "[mcp][nodes][tool_result]") {
  Pipeline p(64, 1);
  const auto one = p.Call("one");
  p.Poll(one);
  const auto two = p.Call("two");
  p.Poll(two);
  p.Complete(one);
  p.Complete(two);
  REQUIRE(p.Poll().size() == 1);
  REQUIRE(p.Poll().size() == 1);
}
TEST_CASE("MCP pickup delivers resolver submission and transport errors", "[mcp][nodes][tool_result]") {
  Pipeline p;
  auto report = p.Submit({.status = AssistantStatus::ERROR, .error = "read-only"});
  auto results = p.Poll(report);
  REQUIRE(results.size() == 1);
  REQUIRE(results[0].is_error);
  REQUIRE(results[0].error == "read-only");
  REQUIRE_FALSE(results[0].action.call_id.empty());
  p.service->accept_requests = false;
  report = p.Call("rejected");
  REQUIRE(report.status == AssistantStatus::ERROR);
  REQUIRE(p.Poll(report).front().is_error);
  p.service->accept_requests = true;
  report = p.Call("transport");
  p.Complete(report, true);
  results = p.Poll(report);
  REQUIRE(results.front().error == "DEVICE_UNREACHABLE");
}
TEST_CASE("MCP pickup correlates resource replies", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const auto report = p.Submit(
    {.kind = McpResolvedActionKind::READ_RESOURCE, .server_id = "home", .uri = "resource://x"}
  );
  mcp::McpReadResourceResultEvent result;
  result.call_id = report.action.call_id;
  result.text = "plain resource text";
  p.service->EmitReadCompletion(report.request_token, result);
  const auto out = p.Poll(report);
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].content_json == "plain resource text");
  REQUIRE(out[0].action.uri == "resource://x");
}
TEST_CASE("MCP overflow produces failures instead of losing pending requests", "[mcp][nodes][tool_result]") {
  Pipeline p(1);
  const auto one = p.Call("one");
  p.Poll(one);
  const auto two = p.Call("two");
  p.Complete(one);
  p.Complete(two);
  const auto out = p.Poll(two);
  REQUIRE(out.size() == 2);
  REQUIRE(out[0].action.context_json == "two");
  REQUIRE(out[1].action.context_json == "one");
  REQUIRE(out[1].is_error);
  REQUIRE(p.Poll().empty());
}
TEST_CASE("MCP reset cancels requests and ignores late replies", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const auto report = p.Call("stale");
  p.Poll(report);
  p.pickup.node->Reset();
  p.Complete(report);
  REQUIRE(p.Poll().empty());
  REQUIRE(p.service->cancelled_tokens.size() == 1);
}

TEST_CASE("MCP pickup completes a subscription right away", "[mcp][nodes][tool_result]") {
  Pipeline p;
  const auto kind = GENERATE(McpResolvedActionKind::SUBSCRIBE_RESOURCE, McpResolvedActionKind::UNSUBSCRIBE_RESOURCE);
  CAPTURE(static_cast<int>(kind));
  const auto report = p.Submit({.kind = kind, .server_id = "home", .uri = "resource://speaker/state"});
  REQUIRE(report.submitted);
  REQUIRE(report.request_token == 0);
  const auto completions = p.Poll(report);
  REQUIRE(completions.size() == 1);
  REQUIRE(completions[0].action.kind == kind);
  REQUIRE_FALSE(completions[0].is_error);
  REQUIRE(p.Poll().empty());
}
