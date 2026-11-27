#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes.h"

#include <catch.hpp>

#include <limits>

using namespace giga_edge;

namespace {

void PushBatch(NodeContext& ctx, const std::string& input, const std::vector<AssistantChunk>& chunks) {
  for (size_t i = 0; i < chunks.size(); ++i) {
    const bool last = i + 1 == chunks.size();
    NodePush<AssistantChunk>(ctx, input, chunks[i], last, last ? std::numeric_limits<int32_t>::max() : 0);
  }
}

AssistantChunk Text(const std::string& text, const std::string& raw = "") {
  return MakeTextChunk(text, raw.empty() ? text : raw);
}

} // namespace

TEST_CASE("assistant_result_to_text speaks only a finished reply", "[assistant][node][result]") {
  auto ctx = CreateNode("assistant_result_to_text", "");

  NodePush(ctx, "assistant_result", AssistantResult{});
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  AssistantResult with_call;
  with_call.is_final = true;
  with_call.text = "ok";
  with_call.tool_calls.push_back(ToolCall{.name = "set"});
  NodePush(ctx, "assistant_result", with_call);
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  AssistantResult reply;
  reply.is_final = true;
  reply.text = "Включил свет";
  NodePush(ctx, "assistant_result", reply);
  REQUIRE(NodeGet<std::string>(ctx, "text") == "Включил свет");

  reply.finish_reason = StreamFinishReason::CANCELLED;
  NodePush(ctx, "assistant_result", reply);
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  AssistantResult failed;
  failed.is_final = true;
  failed.status = AssistantStatus::ERROR;
  failed.text = "oops";
  NodePush(ctx, "assistant_result", failed);
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());
}

TEST_CASE("assistant_result_to_text speaks the fallback on errors only", "[assistant][node][result]") {
  auto ctx = CreateNode("assistant_result_to_text", "fallback_text: Не получилось");

  AssistantResult failed;
  failed.is_final = true;
  failed.status = AssistantStatus::ERROR;
  NodePush(ctx, "assistant_result", failed);
  REQUIRE(NodeGet<std::string>(ctx, "text") == "Не получилось");

  failed.finish_reason = StreamFinishReason::CANCELLED;
  NodePush(ctx, "assistant_result", failed);
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  failed.is_final = false;
  failed.finish_reason = StreamFinishReason::NONE;
  NodePush(ctx, "assistant_result", failed);
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  AssistantResult reply;
  reply.is_final = true;
  reply.text = "Готово";
  NodePush(ctx, "assistant_result", reply);
  REQUIRE(NodeGet<std::string>(ctx, "text") == "Готово");
}

TEST_CASE("result: idle ticks are not final and carry the runner phase", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "", {}, {}, {}, {}, nullptr, {"text"});
  PushBatch(ctx, "stream_chunk", {MakeIdleChunk(RunnerPhase::PREFILLING)});
  const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.status == AssistantStatus::OK);
  REQUIRE_FALSE(result.is_final);
  REQUIRE(result.phase == RunnerPhase::PREFILLING);
  REQUIRE(result.text.empty());
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());
}

TEST_CASE("truncated tool generation becomes an error and is spoken as the fallback", "[assistant][node][result]") {
  auto accumulator = CreateNode("stream_to_assistant_result", "");
  auto final = MakeFinalChunk(StreamFinishReason::MAX_TOKENS);
  final.meta.request_id = "query";
  auto call = MakeToolCallChunk(ToolCall{.name = "set", .arguments_json = "{}"});
  call.meta.request_id = "query";
  PushBatch(accumulator, "stream_chunk", {call, final});
  const auto result = NodeGet<AssistantResult>(accumulator, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.status == AssistantStatus::ERROR);
  REQUIRE(result.tool_calls.empty());
  REQUIRE(result.finish_reason == StreamFinishReason::MAX_TOKENS);

  auto guard = CreateNode("tool_loop_guard", "local_debug_enabled: true");
  NodePush(guard, "user_query", UserQuery{.request_id = "query", .text = "Включи свет"}, false, 0);
  NodePush(guard, "assistant_result", result);
  const auto failed = NodeGet<AssistantResult>(guard, "assistant_result");
  REQUIRE(failed.is_final);
  REQUIRE(failed.status == AssistantStatus::ERROR);
  REQUIRE(failed.tool_calls.empty());
  REQUIRE(failed.text.empty());
  REQUIRE_FALSE(failed.error.empty());
  auto speech = CreateNode("assistant_result_to_text", "fallback_text: Не удалось выполнить команду");
  NodePush(speech, "assistant_result", failed);
  REQUIRE(NodeGet<std::string>(speech, "text") == "Не удалось выполнить команду");

  PushBatch(accumulator, "stream_chunk", {Text("Часть ответа"), final});
  const auto plain = NodeGet<AssistantResult>(accumulator, "assistant_result");
  REQUIRE(plain.status == AssistantStatus::OK);
  REQUIRE(plain.text == "Часть ответа");
}

TEST_CASE("result: text deltas accumulate across ticks and finalize", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "", {}, {}, {}, {}, nullptr, {"text"});
  PushBatch(ctx, "stream_chunk", {Text("Вкл"), Text("ючил ")});
  auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE_FALSE(result.is_final);
  REQUIRE(result.text.empty());
  NodeGet<std::string>(ctx, "text");

  AssistantChunk final = MakeFinalChunk(StreamFinishReason::EOS);
  final.meta.request_id = "r1";
  PushBatch(ctx, "stream_chunk", {Text("свет."), final});
  result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.status == AssistantStatus::OK);
  REQUIRE(result.request_id == "r1");
  REQUIRE(result.text == "Включил свет.");
  REQUIRE(result.raw_text == "Включил свет.");
  REQUIRE(result.finish_reason == StreamFinishReason::EOS);
  REQUIRE(NodeGet<std::string>(ctx, "text") == "Включил свет.");

  PushBatch(ctx, "stream_chunk", {Text("next")});
  result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.text.empty());
}

TEST_CASE("result: tool calls are collected and raw text keeps the call bytes", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "");
  ToolCall call{.name = "set", .arguments_json = "{}", .raw_text = "<f_set>"};
  PushBatch(
    ctx,
    "stream_chunk",
    {Text("", "<f_"), Text("", "set>"), MakeToolCallChunk(call), MakeFinalChunk(StreamFinishReason::STOP_SEQUENCE)}
  );
  const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.tool_calls.size() == 1);
  REQUIRE(result.tool_calls[0].name == "set");
  REQUIRE(result.raw_text == "<f_set>");
  REQUIRE(result.text.empty());
}

TEST_CASE("result: an error chunk ends the answer with an error", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "", {}, {}, {}, {}, nullptr, {"text"});
  PushBatch(ctx, "stream_chunk", {Text("partial"), MakeErrorChunk("boom")});
  const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.status == AssistantStatus::ERROR);
  REQUIRE(result.error == "boom");
  REQUIRE(result.text == "partial");
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());
}

TEST_CASE("result: chunks of the next request in the same batch keep the final", "[assistant][node][result]") {
  const auto of = [](AssistantChunk chunk, const std::string& request_id) {
    chunk.meta.request_id = request_id;
    return chunk;
  };
  auto ctx = CreateNode("stream_to_assistant_result", "", {}, {}, {}, {}, nullptr, {"text"});

  SECTION("answer") {
    PushBatch(
      ctx,
      "stream_chunk",
      {of(Text("Включил свет."), "a"), of(MakeFinalChunk(StreamFinishReason::EOS), "a"), of(MakeIdleChunk(), "b")}
    );
    const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
    REQUIRE(result.is_final);
    REQUIRE(result.request_id == "a");
    REQUIRE(result.status == AssistantStatus::OK);
    REQUIRE(result.text == "Включил свет.");
    REQUIRE(NodeGet<std::string>(ctx, "text") == "Включил свет.");
  }

  SECTION("error") {
    PushBatch(ctx, "stream_chunk", {of(MakeErrorChunk("scene unavailable"), "a"), of(MakeIdleChunk(), "b")});
    const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
    REQUIRE(result.is_final);
    REQUIRE(result.request_id == "a");
    REQUIRE(result.status == AssistantStatus::ERROR);
    REQUIRE(result.error == "scene unavailable");
    REQUIRE(NodeGet<std::string>(ctx, "text").empty());
  }

  SECTION("the next request goes on in the next batch") {
    PushBatch(ctx, "stream_chunk", {of(MakeFinalChunk(StreamFinishReason::EOS), "a"), of(Text("Вы"), "b")});
    REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").request_id == "a");
    NodeGet<std::string>(ctx, "text");
    PushBatch(ctx, "stream_chunk", {of(Text("ключил."), "b"), of(MakeFinalChunk(StreamFinishReason::EOS), "b")});
    const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
    REQUIRE(result.is_final);
    REQUIRE(result.request_id == "b");
    REQUIRE(result.text == "Выключил.");
    REQUIRE(NodeGet<std::string>(ctx, "text") == "Выключил.");
  }

  SECTION("of two finals the later one is the result") {
    PushBatch(
      ctx,
      "stream_chunk",
      {of(MakeFinalChunk(StreamFinishReason::CANCELLED), "a"), of(MakeErrorChunk("scene unavailable"), "b")}
    );
    const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
    REQUIRE(result.request_id == "b");
    REQUIRE(result.status == AssistantStatus::ERROR);
  }
}

TEST_CASE("result: emit_partial exposes the running text", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "emit_partial: true");
  PushBatch(ctx, "stream_chunk", {Text("a"), Text("b")});
  const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE_FALSE(result.is_final);
  REQUIRE(result.text == "ab");
}

TEST_CASE("result: cancellation preserves diagnostics but never emits final text", "[assistant][node][result]") {
  auto ctx = CreateNode("stream_to_assistant_result", "", {}, {}, {}, {}, nullptr, {"text"});
  PushBatch(ctx, "stream_chunk", {Text("unfinished"), MakeFinalChunk(StreamFinishReason::CANCELLED)});
  const auto result = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(result.is_final);
  REQUIRE(result.finish_reason == StreamFinishReason::CANCELLED);
  REQUIRE(result.raw_text == "unfinished");
  REQUIRE(NodeGet<std::string>(ctx, "text").empty());

  PushBatch(ctx, "stream_chunk", {Text("new"), MakeFinalChunk(StreamFinishReason::EOS)});
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").text == "new");
  REQUIRE(NodeGet<std::string>(ctx, "text") == "new");
}

TEST_CASE("sentences: cancellation discards pending speech and permits a replacement", "[assistant][node][sentences]") {
  auto ctx = CreateNode("stream_to_sentences", "");
  PushBatch(ctx, "stream_chunk", {Text("unfinished")});
  REQUIRE(NodeGet<std::string>(ctx, "sentence").empty());
  PushBatch(ctx, "stream_chunk", {MakeFinalChunk(StreamFinishReason::CANCELLED)});
  REQUIRE(NodeGet<std::string>(ctx, "sentence").empty());
  PushBatch(
    ctx,
    "stream_chunk",
    {Text("old"), MakeFinalChunk(StreamFinishReason::CANCELLED), Text("new"), MakeFinalChunk(StreamFinishReason::EOS)}
  );
  REQUIRE(NodeGet<std::string>(ctx, "sentence") == "new");
}

TEST_CASE("sentences: split on terminators and flush on final", "[assistant][node][sentences]") {
  auto ctx = CreateNode("stream_to_sentences", "");
  PushBatch(ctx, "stream_chunk", {Text("Сегодня в Москве ясно. ")});
  REQUIRE(NodeGet<std::string>(ctx, "sentence") == "Сегодня в Москве ясно. ");
  PushBatch(ctx, "stream_chunk", {Text("Завтра обещают")});
  REQUIRE(NodeGet<std::string>(ctx, "sentence").empty());
  PushBatch(ctx, "stream_chunk", {Text(" дождь"), MakeFinalChunk(StreamFinishReason::EOS)});
  REQUIRE(NodeGet<std::string>(ctx, "sentence") == "Завтра обещают дождь");
}

TEST_CASE("sentences: never breaks inside a code point", "[assistant][node][sentences]") {
  auto ctx = CreateNode("stream_to_sentences", "");
  const std::string word = "Погода прекрасная сегодня всё хорошо.";
  const size_t split = word.find("всё") + 4;
  PushBatch(ctx, "stream_chunk", {Text(word.substr(0, split))});
  const auto first = NodeGet<std::string>(ctx, "sentence");
  PushBatch(ctx, "stream_chunk", {Text(word.substr(split)), MakeFinalChunk(StreamFinishReason::EOS)});
  const auto second = NodeGet<std::string>(ctx, "sentence");
  REQUIRE(first + second == word);
}

TEST_CASE("sentences: error clears the buffer", "[assistant][node][sentences]") {
  auto ctx = CreateNode("stream_to_sentences", "");
  PushBatch(ctx, "stream_chunk", {Text("half"), MakeErrorChunk("x")});
  REQUIRE(NodeGet<std::string>(ctx, "sentence").empty());
  PushBatch(ctx, "stream_chunk", {Text("Fresh start."), MakeFinalChunk(StreamFinishReason::EOS)});
  REQUIRE(NodeGet<std::string>(ctx, "sentence") == "Fresh start.");
}

TEST_CASE("merge: idle streams collapse, content and finals pass through", "[assistant][node][merge]") {
  auto ctx = CreateNode("stream_merge", "");
  NodePush<AssistantChunk>(ctx, "first", MakeIdleChunk(), true, 0);
  PushBatch(ctx, "second", {MakeIdleChunk(RunnerPhase::GENERATING)});
  auto out = NodeGetBatch<AssistantChunk>(ctx, "stream_chunk");
  REQUIRE(out.size() == 1);
  REQUIRE(out[0].meta.phase == RunnerPhase::GENERATING);
  REQUIRE(std::holds_alternative<std::monostate>(out[0].payload));

  NodePush<AssistantChunk>(ctx, "first", MakeIdleChunk(), true, 0);
  PushBatch(ctx, "second", {Text("cloud"), MakeFinalChunk(StreamFinishReason::COMPLETED)});
  out = NodeGetBatch<AssistantChunk>(ctx, "stream_chunk");
  REQUIRE(out.size() == 2);
  REQUIRE(ChunkText(out[0])->text == "cloud");
  REQUIRE(out[1].meta.is_final);
}

TEST_CASE("gate: closed gate turns a request into an idle tick", "[assistant][node][gate]") {
  auto ctx = CreateNode("request_gate", "");
  AssistantRequest request;
  request.kind = AssistantRequestKind::GENERATE;
  request.request_id = "r1";
  request.messages.push_back({MessageRole::USER, "q"});
  NodePush(ctx, "open", false, true, 0);
  NodePush(ctx, "assistant_request", request);
  auto out = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(out.kind == AssistantRequestKind::NONE);
  REQUIRE(out.request_id == "r1");
  REQUIRE(out.messages.empty());

  NodePush(ctx, "open", true, true, 0);
  NodePush(ctx, "assistant_request", request);
  out = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(out.kind == AssistantRequestKind::GENERATE);
  REQUIRE(out.messages.size() == 1);
}

TEST_CASE("guard: breaks the loop after the configured rounds", "[assistant][node][guard]") {
  auto ctx = CreateNode("tool_loop_guard", "max_tool_rounds: 1");
  const auto call = [](const std::string& request_id, const std::string& call_id) {
    AssistantResult r;
    r.request_id = request_id;
    r.is_final = true;
    r.status = AssistantStatus::OK;
    r.tool_calls.push_back(ToolCall{.id = call_id, .name = "set"});
    return r;
  };

  NodePush(ctx, "user_query", UserQuery{.request_id = "r1", .text = "go"}, true, 0);
  NodePush(ctx, "assistant_result", call("r1", "c1"));
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").tool_calls.size() == 1);

  NodePush(ctx, "user_query", UserQuery{}, true, 0);
  NodePush(ctx, "assistant_result", call("c1", "c2"));
  auto out = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Tool loop limit exceeded");
  REQUIRE(out.tool_calls.empty());
  REQUIRE(out.text.empty());

  NodePush(ctx, "user_query", UserQuery{.request_id = "r2", .text = "again"}, true, 0);
  NodePush(ctx, "assistant_result", call("r2", "c3"));
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").tool_calls.size() == 1);
}

TEST_CASE("guard: the same call again in one request breaks the loop at once", "[assistant][node][guard]") {
  auto ctx = CreateNode("tool_loop_guard", "break_on_repeated_call: true");
  const auto call = [](const std::string& request_id, const std::string& call_id, const std::string& args) {
    AssistantResult result;
    result.request_id = request_id;
    result.is_final = true;
    result.status = AssistantStatus::OK;
    result.tool_calls.push_back(ToolCall{.id = call_id, .name = "set", .arguments_json = args});
    return result;
  };

  NodePush(ctx, "user_query", UserQuery{.request_id = "r1", .text = "go"}, true, 0);
  NodePush(ctx, "assistant_result", call("r1", "c1", R"({"value":"255;18;0"})"));
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").tool_calls.size() == 1);

  NodePush(ctx, "user_query", UserQuery{}, true, 0);
  NodePush(ctx, "assistant_result", call("c1", "c2", R"({"value":"0;0;255"})"));
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").tool_calls.size() == 1);

  NodePush(ctx, "user_query", UserQuery{}, true, 0);
  NodePush(ctx, "assistant_result", call("c2", "c3", R"({"value":"255;18;0"})"));
  const auto out = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Repeated tool call");
  REQUIRE(out.tool_calls.empty());

  NodePush(ctx, "user_query", UserQuery{.request_id = "r2", .text = "again"}, true, 0);
  NodePush(ctx, "assistant_result", call("r2", "c4", R"({"value":"255;18;0"})"));
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").tool_calls.size() == 1);
}

TEST_CASE("guard: a warm failure before any query is not passed on", "[assistant][node][guard]") {
  auto ctx = CreateNode("tool_loop_guard", "");
  AssistantResult warm;
  warm.request_id = "warm:v1";
  warm.is_final = true;
  warm.status = AssistantStatus::ERROR;
  warm.error = "Prompt does not fit";
  NodePush(ctx, "user_query", UserQuery{}, true, 0);
  NodePush(ctx, "assistant_result", warm);
  const auto out = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE_FALSE(out.is_final);
  REQUIRE(out.status == AssistantStatus::OK);
}

TEST_CASE("stub: emits an empty result", "[assistant][node][stub]") {
  auto ctx = CreateNode("assistant_result_stub", "");
  NodePush<std::vector<float>>(ctx, "input", {0.0f});
  const auto out = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(out.status == AssistantStatus::OK);
  REQUIRE_FALSE(out.is_final);
}

TEST_CASE("stub: session events without the button input are refused", "[assistant][node][stub]") {
  REQUIRE_THROWS_WITH(
    CreateNode("assistant_result_stub", "", {}, {}, {}, {}, nullptr, {"events"}), Catch::Contains("manual_activation")
  );
}

TEST_CASE("stub: the button gets an empty session, a reset closes the session left open", "[assistant][node][stub]") {
  auto ctx = CreateNode("assistant_result_stub", "", {}, {"manual_activation"}, {}, {}, nullptr, {"events"});
  const auto tick = [&ctx](bool manual) {
    NodePush(ctx, "manual_activation", manual, true, 0);
    NodePush<std::vector<float>>(ctx, "input", {0.0f});
    NodeGet<AssistantResult>(ctx, "assistant_result");
    return NodeGet<AssistantSessionEvents>(ctx, "events");
  };
  REQUIRE(tick(false).empty());
  auto events = tick(true);
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].kind == AssistantSessionEvent::Kind::STARTED);
  REQUIRE(events[0].manual);
  REQUIRE(events[1].kind == AssistantSessionEvent::Kind::REPLY);

  NodePushEvent(ctx, Event{.event = ResetEvent{}});
  events = tick(true);
  REQUIRE(events.size() == 3);
  REQUIRE(events[0].kind == AssistantSessionEvent::Kind::REPLY);
  REQUIRE(events[1].kind == AssistantSessionEvent::Kind::STARTED);
  REQUIRE(tick(false).empty());
}

TEST_CASE("assistant_result_relay passes the result unchanged", "[assistant][node][result]") {
  auto ctx = CreateNode("assistant_result_relay", "");
  AssistantResult result;
  result.request_id = "q-1";
  result.text = "Включил свет.";
  result.is_final = true;
  result.tool_calls.push_back({.name = "apply_state"});
  NodePush(ctx, "input", result);

  const auto out = NodeGet<AssistantResult>(ctx, "assistant_result");
  REQUIRE(out.request_id == "q-1");
  REQUIRE(out.text == "Включил свет.");
  REQUIRE(out.is_final);
  REQUIRE(out.tool_calls.size() == 1);
  REQUIRE(out.tool_calls.front().name == "apply_state");
}

TEST_CASE("assistant_result_relay attaches the turn trace when there is one", "[assistant][node][result]") {
  auto ctx = CreateNode("assistant_result_relay", "", {}, {"trace"});
  AssistantResult result{.request_id = "q-1", .is_final = true};
  NodePush(ctx, "trace", TurnTrace{.request_id = "fc-1", .user = "свет"}, true, 0);
  NodePush(ctx, "input", result);
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").trace.request_id == "fc-1");

  NodePush(ctx, "trace", TurnTrace{}, true, 0);
  NodePush(ctx, "input", result);
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").trace.request_id.empty());

  result.trace.request_id = "own";
  NodePush(ctx, "trace", TurnTrace{}, true, 0);
  NodePush(ctx, "input", result);
  REQUIRE(NodeGet<AssistantResult>(ctx, "assistant_result").trace.request_id == "own");

  auto unwired = CreateNode("assistant_result_relay", "");
  NodePush(unwired, "input", result);
  REQUIRE(NodeGet<AssistantResult>(unwired, "assistant_result").trace.request_id == "own");
}

namespace {

struct BatchGuard {
  explicit BatchGuard(const std::string& params)
    : ctx(CreateNode("tool_loop_guard", params, {}, {"tool_result", "cancel", "dialogue_reset"})) {}

  AssistantResult Tick(
    const AssistantResult& result,
    const ToolResult& done = {},
    const UserQuery& query = {},
    bool cancel = false,
    bool reset = false
  ) {
    NodePush(ctx, "dialogue_reset", reset, true, 0);
    NodePush(ctx, "cancel", cancel, true, 0);
    NodePush(ctx, "tool_result", done, true, 0);
    NodePush(ctx, "user_query", query, true, 0);
    NodePush(ctx, "assistant_result", result);
    return NodeGet<AssistantResult>(ctx, "assistant_result");
  }

  NodeContext ctx;
};

ToolCall BatchCall(const std::string& id, const std::string& args) {
  return ToolCall{.id = id, .name = "set", .arguments_json = args, .raw_text = "<f_set>" + args};
}

AssistantResult Batch(const std::string& request_id, std::vector<ToolCall> calls) {
  AssistantResult r;
  r.request_id = request_id;
  r.is_final = true;
  r.finish_reason = StreamFinishReason::STOP_SEQUENCE;
  r.raw_text = "raw batch";
  r.tool_calls = std::move(calls);
  r.usage.generated_tokens = 7;
  return r;
}

ToolResult Done(const std::string& call_id) {
  return ToolResult{.call_id = call_id, .name = "set", .content = R"({"status": "success"})", .is_ready = true};
}

AssistantResult Reply(const std::string& request_id) {
  AssistantResult r;
  r.request_id = request_id;
  r.is_final = true;
  r.text = "Готово.";
  return r;
}

} // namespace

TEST_CASE(
  "guard: a batch goes one call at a time, the next on the result of the previous", "[assistant][node][guard]"
) {
  BatchGuard guard("{parallel_tool_calls: true, break_on_repeated_call: true}");
  const auto batch = Batch(
    "q",
    {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b"), BatchCall("q/tool/3", "a"), BatchCall("q/tool/4", "c")}
  );

  auto out = guard.Tick(batch, {}, UserQuery{.request_id = "q", .text = "go"});
  REQUIRE(out.is_final);
  REQUIRE(out.request_id == "q");
  REQUIRE(out.tool_calls.size() == 1);
  REQUIRE(out.tool_calls[0].id == "q/tool/1");
  REQUIRE(out.raw_text == "<f_set>a");
  REQUIRE(out.text.empty());
  // An exact repeat in the batch (q/tool/3) runs once.
  REQUIRE(out.queued_calls == 2);
  REQUIRE(out.usage.generated_tokens == 7);

  // A foreign result and a tick without a result do not release the next call.
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/9")).is_final);
  REQUIRE_FALSE(guard.Tick({}).is_final);

  out = guard.Tick({}, Done("q/tool/1"));
  REQUIRE(out.request_id == "q/tool/1");
  REQUIRE(out.tool_calls.size() == 1);
  REQUIRE(out.tool_calls[0].id == "q/tool/2");
  REQUIRE(out.raw_text == "<f_set>b");
  REQUIRE(out.queued_calls == 1);
  REQUIRE(out.usage.generated_tokens == 0);

  out = guard.Tick({}, Done("q/tool/2"));
  REQUIRE(out.request_id == "q/tool/2");
  REQUIRE(out.tool_calls[0].id == "q/tool/4");
  REQUIRE(out.queued_calls == 0);

  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/4")).is_final);
  // The next generation follows the last call of the batch.
  REQUIRE(guard.Tick(Reply("q/tool/4")).text == "Готово.");

  // A call from the previous batch in a new batch of the same request is a loop.
  out = guard.Tick(Batch("q/tool/4", {BatchCall("q/tool/5", "d"), BatchCall("q/tool/6", "b")}));
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Repeated tool call");
  REQUIRE(out.tool_calls.empty());
}

TEST_CASE("guard: a batch cut by max_tool_calls ends the turn with the limit error", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true, max_tool_calls: 2}");
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b"), BatchCall("q/tool/3", "c")}),
    {},
    UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(out.tool_calls[0].id == "q/tool/1");
  REQUIRE(out.queued_calls == 2);
  out = guard.Tick({}, Done("q/tool/1"));
  REQUIRE(out.tool_calls[0].id == "q/tool/2");
  // The cut-off call holds the generation: after the result comes the limit error, not a new round.
  REQUIRE(out.queued_calls == 1);
  out = guard.Tick({}, Done("q/tool/2"));
  REQUIRE(out.is_final);
  REQUIRE(out.request_id == "q/tool/2");
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Tool call limit exceeded");
  REQUIRE(out.tool_calls.empty());
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);

  out = guard.Tick(Batch("r", {BatchCall("r/tool/1", "a")}), {}, UserQuery{.request_id = "r", .text = "again"});
  REQUIRE(out.tool_calls.size() == 1);
}

TEST_CASE("guard: a batch that fits max_tool_calls exactly lets the model answer", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true, max_tool_calls: 1}");
  auto out = guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, UserQuery{.request_id = "q", .text = "go"});
  REQUIRE(out.queued_calls == 0);
  REQUIRE(guard.Tick(Reply("q/tool/1"), Done("q/tool/1")).text == "Готово.");
  out = guard.Tick(Batch("q/tool/1", {BatchCall("q/tool/2", "b")}));
  REQUIRE(out.error == "Tool call limit exceeded");
}

TEST_CASE("guard: max_tool_rounds counts generations of batches", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true, max_tool_rounds: 1}");
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")}), {}, UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(out.tool_calls.size() == 1);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls[0].id == "q/tool/2");
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);
  out = guard.Tick(Batch("q/tool/2", {BatchCall("q/tool/3", "c")}));
  REQUIRE(out.error == "Tool loop limit exceeded");
}

TEST_CASE("guard: exact repeats in a batch run once without break_on_repeated_call", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "a"), BatchCall("q/tool/3", "b")}),
    {},
    UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(out.queued_calls == 1);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls[0].id == "q/tool/3");
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/3")).is_final);
  // Without break_on_repeated_call a repeat from the previous batch is executed.
  out = guard.Tick(Batch("q/tool/3", {BatchCall("q/tool/4", "a")}));
  REQUIRE(out.tool_calls.size() == 1);
}

TEST_CASE("guard: a new query or a dialogue reset drops the rest of a batch", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  const auto batch = Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")});
  REQUIRE(guard.Tick(batch, {}, UserQuery{.request_id = "q", .text = "go"}).queued_calls == 1);
  SECTION("new query") {
    REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1"), UserQuery{.request_id = "r", .text = "stop"}).is_final);
  }
  SECTION("dialogue reset") {
    REQUIRE_FALSE(guard.Tick({}, {}, {}, false, true).is_final);
    REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1")).is_final);
  }
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1")).is_final);
}

TEST_CASE("guard: cancel in the tick of the batch does not start it", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  const auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")}),
    {},
    UserQuery{.request_id = "q", .text = "go"},
    true
  );
  REQUIRE(out.is_final);
  REQUIRE(out.finish_reason == StreamFinishReason::CANCELLED);
  REQUIRE(out.tool_calls.empty());
  REQUIRE_FALSE(IsCommitted(out));
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1")).is_final);
}

TEST_CASE("guard: parallel_tool_calls without the tool_result input fails at creation", "[assistant][node][guard]") {
  REQUIRE_THROWS_WITH(
    CreateNode("tool_loop_guard", "{parallel_tool_calls: true}", {}, {"cancel"}), Catch::Contains("tool_result")
  );
  REQUIRE_NOTHROW(CreateNode("tool_loop_guard", "{}"));
}

TEST_CASE(
  "guard: a batch without the cancel input or a limit without batches fails at creation", "[assistant][node][guard]"
) {
  // Without a cancel the batch would go on acting on devices after a barge-in.
  REQUIRE_THROWS_WITH(
    CreateNode("tool_loop_guard", "{parallel_tool_calls: true}", {}, {"tool_result"}), Catch::Contains("cancel")
  );
  REQUIRE_THROWS_AS(CreateNode("tool_loop_guard", "{max_tool_calls: 3}"), std::invalid_argument);
  REQUIRE_THROWS_WITH(
    CreateNode("tool_loop_guard", "{max_tool_calls: 3}", {}, {"tool_result", "cancel"}),
    Catch::Contains("parallel_tool_calls")
  );
  REQUIRE_NOTHROW(CreateNode("tool_loop_guard", "{max_tool_calls: 0}"));
  REQUIRE_NOTHROW(
    CreateNode("tool_loop_guard", "{parallel_tool_calls: true, max_tool_calls: 3}", {}, {"tool_result", "cancel"})
  );
}

TEST_CASE("guard: a batch cut by the token limit is not run", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  auto cut = Batch("q", {});
  cut.finish_reason = StreamFinishReason::MAX_TOKENS;
  cut.status = AssistantStatus::ERROR;
  cut.error = "Tool generation exceeded its token limit";
  const auto out = guard.Tick(cut, {}, UserQuery{.request_id = "q", .text = "go"});
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Tool generation exceeded its token limit");
  REQUIRE(out.tool_calls.empty());
}

TEST_CASE("guard: cancel drops the calls of a batch not yet released", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")}), {}, UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(out.queued_calls == 1);
  REQUIRE_FALSE(guard.Tick({}, {}, {}, true).is_final);
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1")).is_final);
}

TEST_CASE("guard: without parallel_tool_calls a batch is still an error", "[assistant][node][guard]") {
  BatchGuard guard("{}");
  const auto batch = Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")});
  const auto out = guard.Tick(batch, {}, UserQuery{.request_id = "q", .text = "go"});
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Expected one tool call per step");
  // A single call goes as is: raw_text of the whole generation, no queue.
  BatchGuard single("{}");
  const auto one = single.Tick(
    Batch("q", {BatchCall("q/tool/1", "a")}), {}, UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(one.raw_text == "raw batch");
  REQUIRE(one.queued_calls == 0);
}

TEST_CASE("guard: a stale, repeated or unready result does not release a call", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b"), BatchCall("q/tool/3", "c")}),
    {},
    UserQuery{.request_id = "q", .text = "go"}
  );
  REQUIRE(out.tool_calls.at(0).id == "q/tool/1");
  auto unready = Done("q/tool/1");
  unready.is_ready = false;
  REQUIRE_FALSE(guard.Tick({}, unready).is_final);
  // The result of the next call before the current one: not its turn.
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls.at(0).id == "q/tool/2");
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/1")).is_final);
  out = guard.Tick({}, Done("q/tool/2"));
  REQUIRE(out.tool_calls.at(0).id == "q/tool/3");
  REQUIRE(out.queued_calls == 0);
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/3")).is_final);
}

TEST_CASE("guard: max_tool_calls counts per query, a new query starts over", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true, max_tool_calls: 2}");
  const auto two = [](const std::string& id) {
    return Batch(id, {BatchCall(id + "/tool/1", "a"), BatchCall(id + "/tool/2", "b")});
  };
  REQUIRE(guard.Tick(two("q"), {}, UserQuery{.request_id = "q", .text = "go"}).queued_calls == 1);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls.at(0).id == "q/tool/2");
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);
  REQUIRE(guard.Tick(Reply("q/tool/2")).text == "Готово.");

  auto out = guard.Tick(two("r"), {}, UserQuery{.request_id = "r", .text = "again"});
  REQUIRE(out.tool_calls.at(0).id == "r/tool/1");
  REQUIRE(guard.Tick({}, Done("r/tool/1")).tool_calls.at(0).id == "r/tool/2");
}

TEST_CASE("guard: each call of a batch goes to the history with its own raw text", "[assistant][node][guard]") {
  BatchGuard guard("{parallel_tool_calls: true}");
  auto batch = Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")});
  batch.history_text = "history of the whole generation";
  auto out = guard.Tick(batch, {}, UserQuery{.request_id = "q", .text = "go"});
  REQUIRE(out.raw_text == "<f_set>a");
  REQUIRE(out.history_text == "<f_set>a");
  out = guard.Tick(Reply("q/tool/1"), Done("q/tool/1"));
  REQUIRE(out.history_text == "<f_set>b");
}

namespace {

const std::string kFailedCallReplies
  = "failed_call_reply: Не получилось., failed_call_replies: {UNKNOWN_PARAMETER: Так не умею., BAD_VALUE: Не могу "
    "поставить такое значение.}";

std::string GuardParams(bool parallel, bool break_on_repeated_call, bool replies = true) {
  return std::string("{parallel_tool_calls: ") + (parallel ? "true" : "false") + ", break_on_repeated_call: "
    + (break_on_repeated_call ? "true" : "false") + (replies ? ", " + kFailedCallReplies : "") + "}";
}

ToolResult Failed(const std::string& call_id, const std::string& code) {
  auto result = Done(call_id);
  result.content = R"({"status": "fail", "error": ")" + code + R"("})";
  return result;
}

const UserQuery kGo{.request_id = "q", .text = "go"};

} // namespace

TEST_CASE(
  "guard: a repeat of a failed call ends the turn with the reply for its error code", "[assistant][node][guard]"
) {
  const bool parallel = GENERATE(false, true);
  const bool break_on_repeated_call = GENERATE(false, true);
  BatchGuard guard(GuardParams(parallel, break_on_repeated_call));
  REQUIRE(guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo).tool_calls.at(0).id == "q/tool/1");
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/1", "UNKNOWN_PARAMETER")).is_final);
  auto repeat = Batch("q/tool/1", {BatchCall("q/tool/2", "a")});
  repeat.history_text = "<f_set>a";
  const auto out = guard.Tick(repeat);
  REQUIRE(out.is_final);
  REQUIRE(out.status == AssistantStatus::OK);
  REQUIRE(out.error.empty());
  REQUIRE(out.request_id == "q/tool/1");
  REQUIRE(out.tool_calls.empty());
  REQUIRE(out.text == "Так не умею.");
  // The history gets the reply, raw_text stays what the model generated.
  REQUIRE(out.history_text == "Так не умею.");
  REQUIRE(out.raw_text == "raw batch");
  REQUIRE(out.finish_reason == StreamFinishReason::STOP_SEQUENCE);
  REQUIRE(out.usage.generated_tokens == 7);
  REQUIRE(IsCommitted(out));
}

TEST_CASE("guard: a failed call with an unknown or no error code gets failed_call_reply", "[assistant][node][guard]") {
  const std::string content = GENERATE(
    as<std::string>{},
    R"({"status": "fail", "error": "DEVICE_BUSY"})",
    R"({"status": "fail"})",
    R"({"status": "fail", "error": 7})"
  );
  BatchGuard guard(GuardParams(true, true));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo);
  auto failed = Done("q/tool/1");
  failed.content = content;
  REQUIRE_FALSE(guard.Tick({}, failed).is_final);
  const auto out = guard.Tick(Batch("q/tool/1", {BatchCall("q/tool/2", "a")}));
  REQUIRE(out.status == AssistantStatus::OK);
  REQUIRE(out.text == "Не получилось.");
}

TEST_CASE("guard: after a failed call another call or a reply goes on as usual", "[assistant][node][guard]") {
  const bool parallel = GENERATE(false, true);
  BatchGuard guard(GuardParams(parallel, true));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo);
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/1", "BAD_VALUE")).is_final);
  SECTION("another call") {
    const auto out = guard.Tick(Batch("q/tool/1", {BatchCall("q/tool/2", "b")}));
    REQUIRE(out.status == AssistantStatus::OK);
    REQUIRE(out.tool_calls.at(0).id == "q/tool/2");
    REQUIRE(out.text.empty());
    REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);
    REQUIRE(guard.Tick(Reply("q/tool/2")).text == "Готово.");
  }
  SECTION("a reply") {
    const auto out = guard.Tick(Reply("q/tool/1"));
    REQUIRE(out.text == "Готово.");
    REQUIRE(out.history_text.empty());
  }
}

TEST_CASE(
  "guard: with failed_call_reply a repeat of a call that did not fail follows break_on_repeated_call",
  "[assistant][node][guard]"
) {
  const bool parallel = GENERATE(false, true);
  const bool break_on_repeated_call = GENERATE(false, true);
  // A success, not JSON, not an object, not the fail status.
  const std::string content = GENERATE(
    as<std::string>{},
    R"({"status": "success"})",
    "fail",
    R"(["fail"])",
    R"({"status": "failed", "error": "BAD_VALUE"})"
  );
  BatchGuard guard(GuardParams(parallel, break_on_repeated_call));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo);
  auto done = Done("q/tool/1");
  done.content = content;
  REQUIRE_FALSE(guard.Tick({}, done).is_final);
  const auto out = guard.Tick(Batch("q/tool/1", {BatchCall("q/tool/2", "a")}));
  REQUIRE(out.text.empty());
  if (break_on_repeated_call) {
    REQUIRE(out.status == AssistantStatus::ERROR);
    REQUIRE(out.error == "Repeated tool call");
    REQUIRE(out.tool_calls.empty());
  } else {
    REQUIRE(out.status == AssistantStatus::OK);
    REQUIRE(out.tool_calls.at(0).id == "q/tool/2");
  }
}

TEST_CASE(
  "guard: without failed_call_reply a repeat of a failed call follows break_on_repeated_call",
  "[assistant][node][guard]"
) {
  const bool parallel = GENERATE(false, true);
  const bool break_on_repeated_call = GENERATE(false, true);
  BatchGuard guard(GuardParams(parallel, break_on_repeated_call, false));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo);
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/1", "UNKNOWN_PARAMETER")).is_final);
  const auto out = guard.Tick(Batch("q/tool/1", {BatchCall("q/tool/2", "a")}));
  REQUIRE(out.text.empty());
  if (break_on_repeated_call) {
    REQUIRE(out.status == AssistantStatus::ERROR);
    REQUIRE(out.error == "Repeated tool call");
    REQUIRE(out.tool_calls.empty());
  } else {
    REQUIRE(out.status == AssistantStatus::OK);
    REQUIRE(out.tool_calls.at(0).id == "q/tool/2");
  }
}

TEST_CASE(
  "guard: a batch with a failed call runs on, a repeat of it gets the reply and runs nothing",
  "[assistant][node][guard]"
) {
  BatchGuard guard(GuardParams(true, true));
  auto out = guard.Tick(
    Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b"), BatchCall("q/tool/3", "c")}), {}, kGo
  );
  REQUIRE(out.queued_calls == 2);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls.at(0).id == "q/tool/2");
  REQUIRE(guard.Tick({}, Failed("q/tool/2", "BAD_VALUE")).tool_calls.at(0).id == "q/tool/3");
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/3", "UNKNOWN_PARAMETER")).is_final);
  const auto next = [&guard](std::vector<std::string> args) {
    std::vector<ToolCall> calls;
    calls.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
      calls.push_back(BatchCall("q/tool/3/tool/" + std::to_string(i + 1), args[i]));
    }
    return guard.Tick(Batch("q/tool/3", std::move(calls)));
  };
  SECTION("the failed call alone") {
    out = next({"b"});
    REQUIRE(out.text == "Не могу поставить такое значение.");
  }
  SECTION("with the call that has been done") {
    out = next({"a", "b"});
    REQUIRE(out.text == "Не могу поставить такое значение.");
  }
  SECTION("with a new call") {
    out = next({"d", "b"});
    REQUIRE(out.text == "Не могу поставить такое значение.");
  }
  SECTION("of two failed calls the first one in the generation") {
    out = next({"c", "b"});
    REQUIRE(out.text == "Так не умею.");
  }
  REQUIRE(out.status == AssistantStatus::OK);
  REQUIRE(out.is_final);
  REQUIRE(out.tool_calls.empty());
  REQUIRE(out.queued_calls == 0);
  // Nothing of the refused generation is queued: a result does not release a call.
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/3/tool/1")).is_final);
}

TEST_CASE(
  "guard: with failed_call_reply a repeat of a done call of a batch is a repeated tool call", "[assistant][node][guard]"
) {
  BatchGuard guard(GuardParams(true, true));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")}), {}, kGo);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls.at(0).id == "q/tool/2");
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/2", "BAD_VALUE")).is_final);
  const auto out = guard.Tick(Batch("q/tool/2", {BatchCall("q/tool/3", "a")}));
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Repeated tool call");
}

TEST_CASE("guard: only the result of the running call of the query marks it failed", "[assistant][node][guard]") {
  BatchGuard guard(GuardParams(true, false));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a"), BatchCall("q/tool/2", "b")}), {}, kGo);
  // A result of another call while q/tool/1 runs.
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/9", "BAD_VALUE")).is_final);
  REQUIRE(guard.Tick({}, Done("q/tool/1")).tool_calls.at(0).id == "q/tool/2");
  REQUIRE_FALSE(guard.Tick({}, Done("q/tool/2")).is_final);
  REQUIRE(guard.Tick(Batch("q/tool/2", {BatchCall("q/tool/3", "a")})).tool_calls.at(0).id == "q/tool/3");

  SECTION("a new query forgets the failed calls") {
    REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/3", "BAD_VALUE")).is_final);
    const auto out = guard.Tick(
      Batch("r", {BatchCall("r/tool/1", "a")}), {}, UserQuery{.request_id = "r", .text = "again"}
    );
    REQUIRE(out.tool_calls.at(0).id == "r/tool/1");
  }
  SECTION("a result of the previous query does not count in the new one") {
    REQUIRE(
      guard.Tick(Batch("r", {BatchCall("r/tool/1", "a")}), {}, UserQuery{.request_id = "r", .text = "again"})
        .tool_calls.at(0)
        .id
      == "r/tool/1"
    );
    REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/3", "BAD_VALUE")).is_final);
    REQUIRE_FALSE(guard.Tick({}, Done("r/tool/1")).is_final);
    REQUIRE(guard.Tick(Batch("r/tool/1", {BatchCall("r/tool/2", "a")})).tool_calls.at(0).id == "r/tool/2");
  }
}

TEST_CASE(
  "guard: a cancelled, failed or cut generation is not answered with failed_call_reply", "[assistant][node][guard]"
) {
  BatchGuard guard(GuardParams(false, false));
  guard.Tick(Batch("q", {BatchCall("q/tool/1", "a")}), {}, kGo);
  REQUIRE_FALSE(guard.Tick({}, Failed("q/tool/1", "BAD_VALUE")).is_final);
  auto repeat = Batch("q/tool/1", {BatchCall("q/tool/2", "a")});
  SECTION("cancel") {
    const auto out = guard.Tick(repeat, {}, {}, true);
    REQUIRE(out.tool_calls.size() == 1);
    REQUIRE(out.text.empty());
  }
  SECTION("generation error") {
    repeat.status = AssistantStatus::ERROR;
    repeat.error = "boom";
    const auto out = guard.Tick(repeat);
    REQUIRE(out.status == AssistantStatus::ERROR);
    REQUIRE(out.error == "boom");
    REQUIRE(out.text.empty());
  }
  SECTION("token limit") {
    repeat.finish_reason = StreamFinishReason::MAX_TOKENS;
    const auto out = guard.Tick(repeat);
    REQUIRE(out.status == AssistantStatus::ERROR);
    REQUIRE(out.error == "Tool call exceeded its token limit");
    REQUIRE(out.text.empty());
  }
}

TEST_CASE(
  "guard: failed_call_replies without failed_call_reply or the tool_result input fails at creation",
  "[assistant][node][guard]"
) {
  REQUIRE_THROWS_WITH(
    CreateNode("tool_loop_guard", "{failed_call_replies: {BAD_VALUE: Нет.}}", {}, {"tool_result"}),
    Catch::Contains("failed_call_reply")
  );
  REQUIRE_THROWS_WITH(CreateNode("tool_loop_guard", "{failed_call_reply: Нет.}"), Catch::Contains("tool_result"));
  REQUIRE_NOTHROW(CreateNode("tool_loop_guard", "{failed_call_reply: Нет.}", {}, {"tool_result"}));
}
