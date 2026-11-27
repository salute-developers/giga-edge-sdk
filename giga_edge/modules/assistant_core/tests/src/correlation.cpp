#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/dialogue.h"
#include "giga_edge/modules/assistant_core/nodes.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("tool results match the pending call exactly", "[assistant][correlation]") {
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "one";
  history.pending.steps.push_back(ToolStep{.call = {.id = "one/tool/1"}});
  ToolResult result{.call_id = "old/tool/1", .is_ready = true};
  REQUIRE_FALSE(MatchesPendingToolResult(history, result));
  result.call_id = "one/tool/1";
  REQUIRE(MatchesPendingToolResult(history, result));
  history.pending.steps.back().has_result = true;
  REQUIRE_FALSE(MatchesPendingToolResult(history, result));
}

TEST_CASE("builder ignores stale completions and suppresses a reset tick", "[assistant][correlation]") {
  auto node = CreateNode("dialogue_request_builder", "", {}, {"dialogue_history", "tool_result", "dialogue_reset"});
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "one";
  history.pending.steps.push_back(ToolStep{.call = {.id = "one/tool/1"}});
  for (const bool reset : {false, true}) {
    NodePush(node, "dialogue_history", history, true, 0);
    NodePush(node, "tool_result", ToolResult{.call_id = reset ? "one/tool/1" : "old", .is_ready = true}, true, 0);
    NodePush(node, "dialogue_reset", reset, true, 0);
    NodePush(node, "user_query", UserQuery{});
    REQUIRE(NodeGet<AssistantRequest>(node, "assistant_request").kind == AssistantRequestKind::NONE);
  }
}

TEST_CASE("state cannot attach an old generation or tool result to a new query", "[assistant][correlation]") {
  auto node = CreateNode("dialogue_state_update", "", {}, {"tool_result"});
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "old";
  AssistantResult old;
  old.request_id = "old";
  old.is_final = true;
  old.tool_calls.push_back(ToolCall{.id = "old/tool/1", .name = "set"});
  NodePush(node, "previous_history", history, true, 0);
  NodePush(node, "tool_result", ToolResult{.call_id = "old/tool/1", .is_ready = true}, true, 0);
  NodePush(node, "user_query", UserQuery{.request_id = "new", .text = "new query"}, true, 0);
  NodePush(node, "assistant_result", old);
  auto next = NodeGet<DialogueHistory>(node, "dialogue_history");
  REQUIRE(next.has_queued);
  REQUIRE(next.queued.request_id == "new");
  REQUIRE(next.queued.steps.empty());
  REQUIRE(next.pending.request_id == "old");

  AssistantResult started;
  started.request_id = "new";
  started.phase = RunnerPhase::PREFILLING;
  NodePush(node, "previous_history", next, true, 0);
  NodePush(node, "tool_result", ToolResult{}, true, 0);
  NodePush(node, "user_query", UserQuery{}, true, 0);
  NodePush(node, "assistant_result", started);
  next = NodeGet<DialogueHistory>(node, "dialogue_history");
  REQUIRE_FALSE(next.has_queued);
  REQUIRE(next.has_pending);
  REQUIRE(next.pending.request_id == "new");
  REQUIRE(next.pending.user == "new query");
  REQUIRE(next.pending.steps.empty());
}

TEST_CASE("stream accumulator isolates requests including replacement within one batch", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "emit_partial: true");
  auto old = MakeTextChunk("old", "old");
  old.meta.request_id = "old";
  NodePush(node, "stream_chunk", old);
  REQUIRE(NodeGet<AssistantResult>(node, "assistant_result").text == "old");
  auto cancel = MakeFinalChunk(StreamFinishReason::CANCELLED);
  cancel.meta.request_id = "old";
  auto fresh = MakeTextChunk("new", "new");
  fresh.meta.request_id = "new";
  NodePush(node, "stream_chunk", cancel, false, 0);
  NodePush(node, "stream_chunk", fresh);
  // The result of the cancelled request comes in the cancel tick, the text of the new one accumulates separately.
  auto result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.request_id == "old");
  REQUIRE(result.finish_reason == StreamFinishReason::CANCELLED);
  REQUIRE(result.raw_text == "old");
  NodePush(node, "stream_chunk", MakeIdleChunk());
  result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE_FALSE(result.is_final);
  REQUIRE(result.request_id == "new");
  REQUIRE(result.text == "new");
  NodePush(node, "stream_chunk", MakeFinalChunk(StreamFinishReason::EOS));
  result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.request_id == "new");
  REQUIRE(result.raw_text == "new");
}

TEST_CASE("compact tool calls get deterministic IDs without replacing native IDs", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "");
  auto one = MakeToolCallChunk(ToolCall{.name = "set"});
  one.meta.request_id = "query";
  NodePush(node, "stream_chunk", one, false, 0);
  NodePush(node, "stream_chunk", MakeToolCallChunk(ToolCall{.id = "native", .name = "read"}), false, 0);
  NodePush(node, "stream_chunk", MakeFinalChunk(StreamFinishReason::EOS));
  const auto result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.tool_calls[0].id == "query/tool/1");
  REQUIRE(result.tool_calls[1].id == "native");
}

TEST_CASE("the final chunk's usage reaches the result", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "");
  auto text = MakeTextChunk("Готово.", "Готово.");
  text.meta.request_id = "query";
  NodePush(node, "stream_chunk", text, false, 0);
  auto final = MakeFinalChunk(StreamFinishReason::EOS);
  final.meta.usage = GenerationUsage{.generated_tokens = 3, .prefill_ms = 12.5, .prompt_label = "act"};
  NodePush(node, "stream_chunk", final, false, 0);
  NodePush(node, "stream_chunk", AssistantChunk{});
  const auto result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.usage.generated_tokens == 3);
  REQUIRE(result.usage.prefill_ms == 12.5);
  REQUIRE(result.usage.prompt_label == "act");
}

TEST_CASE("an error chunk carries its usage, the next request starts without it", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "");
  auto error = MakeErrorChunk("Malformed tool call");
  error.meta.request_id = "a";
  error.meta.usage.generated_tokens = 7;
  NodePush(node, "stream_chunk", error);
  auto result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.status == AssistantStatus::ERROR);
  REQUIRE(result.usage.generated_tokens == 7);

  auto final = MakeFinalChunk(StreamFinishReason::EOS);
  final.meta.request_id = "b";
  final.meta.usage.generated_tokens = 5;
  NodePush(node, "stream_chunk", final, false, 0);
  auto next = MakeTextChunk("x", "x");
  next.meta.request_id = "c";
  NodePush(node, "stream_chunk", next);
  result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.request_id == "b");
  REQUIRE(result.usage.generated_tokens == 5);
  NodePush(node, "stream_chunk", AssistantChunk{});
  result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.request_id == "c");
  REQUIRE_FALSE(result.is_final);
  REQUIRE(result.usage.generated_tokens == 0);
}

TEST_CASE("call ids continue along the hops of a turn instead of nesting", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "");
  auto ids = [&](const std::string& request_id, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      auto call = MakeToolCallChunk(ToolCall{.name = "set"});
      call.meta.request_id = request_id;
      NodePush(node, "stream_chunk", call, false, 0);
    }
    auto final = MakeFinalChunk(StreamFinishReason::EOS);
    final.meta.request_id = request_id;
    NodePush(node, "stream_chunk", final);
    std::vector<std::string> out;
    for (const auto& call : NodeGet<AssistantResult>(node, "assistant_result").tool_calls) {
      out.push_back(call.id);
    }
    return out;
  };
  REQUIRE(ids("query", 1) == std::vector<std::string>{"query/tool/1"});
  REQUIRE(ids("query/tool/1", 1) == std::vector<std::string>{"query/tool/2"});
  REQUIRE(ids("query/tool/2", 2) == std::vector<std::string>{"query/tool/3", "query/tool/4"});
  REQUIRE(ids("odd/tool/x", 1) == std::vector<std::string>{"odd/tool/x/tool/1"});
}

TEST_CASE("guard blocks old replies after reset and rejects unsupported parallel calls", "[assistant][correlation]") {
  auto node = CreateNode("tool_loop_guard", "", {}, {"dialogue_reset"});
  auto tick = [&](bool reset, UserQuery query, AssistantResult result) {
    NodePush(node, "dialogue_reset", reset, true, 0);
    NodePush(node, "user_query", query, true, 0);
    NodePush(node, "assistant_result", result);
    return NodeGet<AssistantResult>(node, "assistant_result");
  };
  AssistantResult result;
  result.request_id = "old";
  result.is_final = true;
  result.tool_calls = {{.id = "old/tool/1", .name = "set"}};
  REQUIRE(tick(false, {.request_id = "old", .text = "set"}, result).tool_calls.size() == 1);
  REQUIRE(tick(true, {}, result).tool_calls.empty());
  REQUIRE(tick(false, {}, result).tool_calls.empty());
  REQUIRE(tick(false, {.request_id = "new", .text = "read"}, result).tool_calls.empty());
  result.request_id = "new";
  result.tool_calls.push_back(ToolCall{.name = "another"});
  result.raw_text = "<f_set><f_another>";
  auto limited = tick(false, {}, result);
  REQUIRE(limited.is_final);
  REQUIRE(limited.status == AssistantStatus::ERROR);
  REQUIRE(limited.error == "Expected one tool call per step");
  REQUIRE(limited.tool_calls.empty());
  REQUIRE(limited.text.empty());
  REQUIRE(limited.raw_text == result.raw_text);
}

TEST_CASE("guard lets the running reply finish when a new query is not taken", "[assistant][correlation]") {
  auto node = CreateNode("tool_loop_guard", "");
  auto tick = [&](UserQuery query, AssistantResult result) {
    NodePush(node, "user_query", query, true, 0);
    NodePush(node, "assistant_result", result);
    return NodeGet<AssistantResult>(node, "assistant_result");
  };
  AssistantResult call;
  call.request_id = "one";
  call.is_final = true;
  call.tool_calls = {{.id = "one/tool/1", .name = "set"}};
  REQUIRE(tick({.request_id = "one", .text = "set"}, call).tool_calls.size() == 1);

  AssistantResult reply;
  reply.request_id = "one/tool/1";
  reply.is_final = true;
  reply.text = "done";
  REQUIRE(tick({.request_id = "two", .text = "read"}, reply).text == "done");

  AssistantResult fresh;
  fresh.request_id = "three";
  fresh.is_final = true;
  fresh.text = "fresh";
  REQUIRE(tick({.request_id = "three", .text = "again"}, fresh).text == "fresh");
  reply.text = "late";
  REQUIRE_FALSE(tick({}, reply).is_final);
}

TEST_CASE("guard correlates followups with native tool IDs", "[assistant][correlation]") {
  auto node = CreateNode("tool_loop_guard", "");
  AssistantResult result;
  result.request_id = "query";
  result.is_final = true;
  result.tool_calls = {{.id = "native-call-id", .name = "set"}};
  NodePush(node, "user_query", UserQuery{.request_id = "query", .text = "set"}, true, 0);
  NodePush(node, "assistant_result", result);
  REQUIRE(NodeGet<AssistantResult>(node, "assistant_result").tool_calls.size() == 1);
  NodePush(node, "user_query", UserQuery{}, true, 0);
  NodePush(node, "assistant_result", result);
  REQUIRE_FALSE(NodeGet<AssistantResult>(node, "assistant_result").is_final);
  result.request_id = "native-call-id";
  result.tool_calls.clear();
  result.text = "done";
  NodePush(node, "user_query", UserQuery{}, true, 0);
  NodePush(node, "assistant_result", result);
  REQUIRE(NodeGet<AssistantResult>(node, "assistant_result").text == "done");
}

TEST_CASE("stream accumulator keeps the history of one request away from the next", "[assistant][correlation]") {
  auto node = CreateNode("stream_to_assistant_result", "");
  auto old = MakeTextChunk("", "");
  std::get<TextDelta>(old.payload).history = "old history";
  old.meta.request_id = "old";
  auto fresh = MakeTextChunk("new", "new");
  fresh.meta.request_id = "new";
  auto history = MakeTextChunk("", "");
  std::get<TextDelta>(history.payload).history = "new history";
  NodePush(node, "stream_chunk", old, false, 0);
  NodePush(node, "stream_chunk", fresh, false, 0);
  NodePush(node, "stream_chunk", history, false, 0);
  NodePush(node, "stream_chunk", MakeFinalChunk(StreamFinishReason::EOS));
  const auto result = NodeGet<AssistantResult>(node, "assistant_result");
  REQUIRE(result.request_id == "new");
  REQUIRE(result.raw_text == "new");
  REQUIRE(result.history_text == "new history");
}
