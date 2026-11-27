#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/nodes/mcp_tool_result/node.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

McpActionCompletion ToolCompletion(const std::string& call_id, std::string structured, std::string content = "") {
  McpActionCompletion completion;
  completion.action = McpResolvedAction{
    .kind = McpResolvedActionKind::CALL_TOOL,
    .source_action_name = "set_volume",
    .server_id = "home",
    .tool_name = "set_volume",
    .call_id = call_id,
  };
  completion.structured_content_json = std::move(structured);
  completion.content_json = std::move(content);
  return completion;
}

ToolResult Tick(NodeContext& node, const McpActionCompletions& completions = {}) {
  NodePush(node, "mcp_completions", completions);
  return NodeGet<ToolResult>(node, "tool_result");
}

} // namespace

TEST_CASE("mcp_tool_result is idle without completions", "[mcp][nodes][tool_result]") {
  auto node = CreateNode("mcp_tool_result", "");
  const auto result = Tick(node);
  REQUIRE_FALSE(result.is_ready);
  REQUIRE_FALSE(IsReadyToolResult(result));
}

TEST_CASE("mcp_tool_result formats tool completions with the call identity", "[mcp][nodes][tool_result]") {
  auto node = CreateNode("mcp_tool_result", "result_format: json_v1");
  auto result = Tick(node, {ToolCompletion("call-1", R"({"level":42})", R"([{"type":"text","text":"ok"}])")});
  REQUIRE(result.is_ready);
  REQUIRE(result.call_id == "call-1");
  REQUIRE(result.name == "set_volume");
  REQUIRE(result.content == R"({"level":42})");

  result = Tick(node, {ToolCompletion("call-2", "", R"([{"type":"text","text":"ok"}])")});
  REQUIRE(result.content == R"([{"text":"ok","type":"text"}])");
}

TEST_CASE("mcp_tool_result formats resource reads", "[mcp][nodes][tool_result]") {
  auto node = CreateNode("mcp_tool_result", "");
  McpActionCompletion read;
  read.action = McpResolvedAction{
    .kind = McpResolvedActionKind::READ_RESOURCE, .server_id = "home", .uri = "resource://speaker/state", .call_id = "r"
  };
  read.content_json = R"({"volume":42})";
  auto result = Tick(node, {read});
  REQUIRE(result.name == "resource://speaker/state");
  REQUIRE(result.content == R"({"volume":42})");

  read.content_json = "plain text";
  REQUIRE(Tick(node, {read}).content == R"({"text":"plain text"})");
}

TEST_CASE("mcp_tool_result turns failures into fail JSON", "[mcp][nodes][tool_result]") {
  auto node = CreateNode("mcp_tool_result", "");
  auto failed = ToolCompletion("call-1", "");
  failed.is_error = true;
  failed.error = "DEVICE_UNREACHABLE";
  auto result = Tick(node, {failed});
  REQUIRE(result.call_id == "call-1");
  REQUIRE(result.content == R"({"error":"DEVICE_UNREACHABLE","status":"fail"})");

  McpActionCompletion rejected;
  rejected.action = McpResolvedAction{.source_action_name = "call_tool", .call_id = "call-2"};
  rejected.is_error = true;
  rejected.error = "No configured MCP server exposes tool: x";
  result = Tick(node, {rejected});
  REQUIRE(result.name == "call_tool");
  REQUIRE(result.content.find("No configured MCP server") != std::string::npos);
}

TEST_CASE(
  "mcp_tool_result emits one completion per tick in order and forgets them on reset", "[mcp][nodes][tool_result]"
) {
  auto node = CreateNode("mcp_tool_result", "");
  REQUIRE(Tick(node, {ToolCompletion("one", "1"), ToolCompletion("two", "2")}).call_id == "one");
  REQUIRE(Tick(node).call_id == "two");
  REQUIRE_FALSE(Tick(node).is_ready);

  Tick(node, {ToolCompletion("three", "3"), ToolCompletion("four", "4")});
  node.node->Reset();
  REQUIRE_FALSE(Tick(node).is_ready);
}

TEST_CASE("mcp_tool_result rejects an unknown result format", "[mcp][nodes][tool_result]") {
  REQUIRE_THROWS(CreateNode("mcp_tool_result", "result_format: missing_v9"));
}
