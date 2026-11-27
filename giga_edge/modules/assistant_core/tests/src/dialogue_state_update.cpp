#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <catch.hpp>

#include <chrono>
#include <thread>

using namespace giga_edge;

namespace {

UserQuery Query(const std::string& text, const std::string& id = "r1") {
  UserQuery q;
  q.request_id = text.empty() ? "" : id;
  q.text = text;
  return q;
}

AssistantResult Idle() {
  return AssistantResult{};
}

AssistantResult Started(const std::string& id = "r1") {
  AssistantResult r;
  r.request_id = id;
  r.phase = RunnerPhase::PREFILLING;
  return r;
}

AssistantResult FinalText(const std::string& raw) {
  AssistantResult r;
  r.request_id = "r1";
  r.is_final = true;
  r.status = AssistantStatus::OK;
  r.raw_text = raw;
  r.text = raw;
  r.finish_reason = StreamFinishReason::EOS;
  return r;
}

AssistantResult FinalCall(const std::string& raw, const std::string& name) {
  AssistantResult r = FinalText(raw);
  r.text.clear();
  r.tool_calls.push_back(ToolCall{.id = "c1", .name = name, .arguments_json = "{}", .raw_text = raw});
  return r;
}

struct StateNode {
  NodeContext ctx;
  explicit StateNode(const std::string& params, std::unordered_set<std::string> outputs = {})
    : ctx(CreateNode(
        "dialogue_state_update",
        params,
        {},
        {"tool_result", "scene_snapshot", "dialogue_reset", "cancel"},
        {},
        {},
        nullptr,
        std::move(outputs)
      )) {}

  DialogueHistory Tick(
    const DialogueHistory& prev,
    const UserQuery& query,
    const AssistantResult& result,
    std::optional<ToolResult> tool = std::nullopt,
    std::optional<SceneSnapshot> scene = std::nullopt,
    std::optional<bool> reset = std::nullopt,
    bool cancel = false
  ) {
    NodePush(ctx, "tool_result", tool.value_or(ToolResult{}), true, 0);
    NodePush(ctx, "scene_snapshot", scene.value_or(SceneSnapshot{}), true, 0);
    NodePush(ctx, "dialogue_reset", reset.value_or(false), true, 0);
    NodePush(ctx, "cancel", cancel, true, 0);
    NodePush(ctx, "previous_history", prev, true, 0);
    NodePush(ctx, "user_query", query, true, 0);
    NodePush(ctx, "assistant_result", result);
    return NodeGet<DialogueHistory>(ctx, "dialogue_history");
  }
};

} // namespace

TEST_CASE("state: a query opens a pending turn with the scene of that moment", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  const auto h = node.Tick(
    DialogueHistory{}, Query("turn on"), Started(), std::nullopt, SceneSnapshot{.text = "lamp\n", .version = "v1"}
  );
  REQUIRE(h.has_pending);
  REQUIRE(h.pending.user == "turn on");
  REQUIRE(h.pending.request_id == "r1");
  REQUIRE(h.pending.scene_text == "lamp");
  REQUIRE(h.pending.scene_version == "v1");
  REQUIRE(h.last_scene_version == "v1");
  REQUIRE(h.turns.empty());
}

TEST_CASE("state: tool call, result and final answer make one turn", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("turn on"), Started());
  h = node.Tick(h, Query(""), FinalCall("<f_set>", "set"));
  REQUIRE(h.has_pending);
  REQUIRE(h.pending.steps.size() == 1);
  REQUIRE(h.pending.steps[0].assistant_raw == "<f_set>");
  REQUIRE(h.pending.steps[0].call.name == "set");
  REQUIRE_FALSE(h.pending.steps[0].has_result);

  h = node.Tick(
    h,
    Query(""),
    Idle(),
    ToolResult{.call_id = "c1", .name = "set", .content = "{\"status\": \"success\"}", .is_ready = true}
  );
  REQUIRE(h.pending.steps[0].has_result);
  REQUIRE(h.pending.steps[0].result.content == "{\"status\": \"success\"}");

  auto final = FinalText("Done.");
  final.request_id = "c1";
  h = node.Tick(h, Query(""), final);
  REQUIRE_FALSE(h.has_pending);
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].user == "turn on");
  REQUIRE(h.turns[0].steps.size() == 1);
  REQUIRE(h.turns[0].assistant == "Done.");
  REQUIRE(h.turns[0].completed);
}

TEST_CASE("state: max_turns 0 keeps the pending hop but no history", "[assistant][node][state]") {
  StateNode node("max_turns: 0");
  auto h = node.Tick(DialogueHistory{}, Query("q"), Started());
  h = node.Tick(h, Query(""), FinalCall("<f_get>", "get"));
  REQUIRE(h.has_pending);
  h = node.Tick(h, Query(""), Idle(), ToolResult{.call_id = "c1", .is_ready = true});
  auto final = FinalText("answer");
  final.request_id = "c1";
  h = node.Tick(h, Query(""), final);
  REQUIRE_FALSE(h.has_pending);
  REQUIRE(h.turns.empty());
}

TEST_CASE("state: overflow trims the history down to trim_to_turns at once", "[assistant][node][state]") {
  StateNode node("{max_turns: 4, trim_to_turns: 2}");
  DialogueHistory h;
  std::vector<size_t> sizes;
  for (int i = 1; i <= 7; ++i) {
    const auto id = "r" + std::to_string(i);
    h = node.Tick(h, Query("q" + std::to_string(i), id), Started(id));
    auto final = FinalText("a" + std::to_string(i));
    final.request_id = id;
    h = node.Tick(h, Query(""), final);
    sizes.push_back(h.turns.size());
  }
  REQUIRE(sizes == std::vector<size_t>{1, 2, 3, 4, 2, 3, 4});
  REQUIRE(h.turns.front().user == "q4");
  REQUIRE(h.turns.back().user == "q7");
}

TEST_CASE("state: trim_to_turns: 0 keeps the turn just finished", "[assistant][node][state]") {
  StateNode node("{max_turns: 2, trim_to_turns: 0}");
  DialogueHistory h;
  for (int i = 1; i <= 3; ++i) {
    const auto id = "r" + std::to_string(i);
    h = node.Tick(h, Query("q" + std::to_string(i), id), Started(id));
    auto final = FinalText("a" + std::to_string(i));
    final.request_id = id;
    h = node.Tick(h, Query(""), final);
  }
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns.front().user == "q3");
}

TEST_CASE("state: window is trimmed and a new query drops an unfinished pending", "[assistant][node][state]") {
  StateNode node("max_turns: 1");
  auto h = node.Tick(DialogueHistory{}, Query("one", "r1"), Started("r1"));
  h = node.Tick(h, Query(""), FinalText("a1"));
  h = node.Tick(h, Query("two", "r2"), Started("r2"));
  auto second = FinalText("a2");
  second.request_id = "r2";
  h = node.Tick(h, Query(""), second);
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].user == "two");

  h = node.Tick(h, Query("three", "r3"), Started("r3"));
  auto call = FinalCall("<f_x>", "x");
  call.request_id = "r3";
  h = node.Tick(h, Query(""), call);
  h = node.Tick(h, Query("four", "r4"), Started("r4"));
  REQUIRE(h.has_pending);
  REQUIRE(h.pending.user == "four");
  REQUIRE(h.pending.steps.empty());
}

TEST_CASE("state: a query the llm did not take stays queued", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("one", "r1"), Started("r1"));
  h = node.Tick(h, Query("two", "r2"), Idle());
  REQUIRE(h.has_queued);
  REQUIRE(h.queued.user == "two");
  REQUIRE(h.pending.user == "one");

  h = node.Tick(h, Query(""), FinalText("a1"));
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].assistant == "a1");
  REQUIRE(h.has_queued);
  REQUIRE_FALSE(h.has_pending);

  h = node.Tick(h, Query("three", "r3"), Started("r3"));
  REQUIRE_FALSE(h.has_queued);
  REQUIRE(h.pending.user == "three");
}

TEST_CASE("state: errors drop the pending turn and reset clears everything", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("q"), Started());
  AssistantResult bad;
  bad.request_id = "r1";
  bad.is_final = true;
  bad.status = AssistantStatus::ERROR;
  h = node.Tick(h, Query(""), bad);
  REQUIRE_FALSE(h.has_pending);

  h = node.Tick(h, Query("q2", "r2"), Started("r2"));
  auto final = FinalText("a");
  final.request_id = "r2";
  h = node.Tick(h, Query(""), final);
  REQUIRE(h.turns.size() == 1);
  h = node.Tick(h, Query(""), Idle(), std::nullopt, std::nullopt, true);
  REQUIRE(h.turns.empty());
  REQUIRE_FALSE(h.has_pending);
}

TEST_CASE("state: a declined request is as if it never was", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("q1"), Started());
  h = node.Tick(h, Query(""), FinalText("a1"));
  REQUIRE(h.turns.size() == 1);

  h = node.Tick(h, Query("q2", "r2"), Started("r2"));
  AssistantResult declined;
  declined.request_id = "r2";
  declined.is_final = true;
  declined.finish_reason = StreamFinishReason::DECLINED;
  h = node.Tick(h, Query(""), declined);
  REQUIRE_FALSE(h.has_pending);
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].user == "q1");
}

TEST_CASE("state: a graph reset clears the history, the tick's query starts anew", "[assistant][node][state]") {
  // The silence timer stops on a reset: the reset itself clears the history, it does not live until the next query.
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("q"), Started());
  h = node.Tick(h, Query(""), FinalText("a"));
  REQUIRE(h.turns.size() == 1);
  NodePushEvent(node.ctx, Event{.event = ResetEvent{}});
  h = node.Tick(h, Query("q2", "r2"), Idle());
  REQUIRE(h.turns.empty());
  REQUIRE(h.has_queued);
  REQUIRE(h.queued.user == "q2");
  h = node.Tick(h, Query(""), Idle());
  REQUIRE(h.has_queued);
}

TEST_CASE("state: cancel drops the unfinished turn, a late tool result stays out", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("включи свет", "r1"), Started("r1"));
  auto done = FinalText("Включил.");
  h = node.Tick(h, Query(""), done);
  REQUIRE(h.turns.size() == 1);

  // The call went to the smart home, the reply will arrive after the cancel.
  h = node.Tick(h, Query("выключи", "r2"), Started("r2"));
  auto call = FinalCall("<f_set>", "set");
  call.request_id = "r2";
  h = node.Tick(h, Query(""), call);
  REQUIRE(h.has_pending);
  h = node.Tick(h, Query(""), Idle(), std::nullopt, std::nullopt, std::nullopt, true);
  REQUIRE_FALSE(h.has_pending);
  REQUIRE(h.turns.size() == 1);
  h = node.Tick(
    h,
    Query(""),
    Idle(),
    ToolResult{.call_id = "c1", .name = "set", .content = "{\"status\": \"success\"}", .is_ready = true}
  );
  REQUIRE_FALSE(h.has_pending);
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].user == "включи свет");

  // A query the LLM has not taken yet is dropped too.
  h = node.Tick(h, Query("ярче", "r3"), Idle());
  REQUIRE(h.has_queued);
  h = node.Tick(h, Query(""), Idle(), std::nullopt, std::nullopt, std::nullopt, true);
  REQUIRE_FALSE(h.has_queued);
}

TEST_CASE("state: cancel variations keep finished turns intact", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  const auto finish = [&node](DialogueHistory h, const std::string& user, const std::string& id) {
    h = node.Tick(h, Query(user, id), Started(id));
    auto final = FinalText("ok " + user);
    final.request_id = id;
    return node.Tick(h, Query(""), final);
  };
  auto h = finish(DialogueHistory{}, "раз", "r1");
  h = finish(h, "два", "r2");
  REQUIRE(h.turns.size() == 2);

  SECTION("cancel without an unfinished turn changes nothing") {
    const auto after = node.Tick(h, Query(""), Idle(), std::nullopt, std::nullopt, std::nullopt, true);
    REQUIRE(after.turns.size() == 2);
    REQUIRE_FALSE(after.has_pending);
    REQUIRE_FALSE(after.has_queued);
  }

  SECTION("cancel in the middle of a tool chain drops the whole unfinished turn") {
    h = node.Tick(h, Query("три", "r3"), Started("r3"));
    auto call = FinalCall("<f_set>", "set");
    call.request_id = "r3";
    h = node.Tick(h, Query(""), call);
    h = node.Tick(h, Query(""), Idle(), ToolResult{.call_id = "c1", .name = "set", .content = "{}", .is_ready = true});
    REQUIRE(h.pending.steps.size() == 1);
    h = node.Tick(h, Query(""), Idle(), std::nullopt, std::nullopt, std::nullopt, true);
    REQUIRE_FALSE(h.has_pending);
    REQUIRE(h.turns.size() == 2);
    // A late final of the abandoned turn does not attach to anything.
    auto late = FinalText("поздний");
    late.request_id = "c1";
    h = node.Tick(h, Query(""), late);
    REQUIRE(h.turns.size() == 2);
  }

  SECTION("cancel and a new query in one tick: the new one is queued and becomes the next turn") {
    h = node.Tick(h, Query("три", "r3"), Started("r3"));
    h = node.Tick(h, Query("четыре", "r4"), Idle(), std::nullopt, std::nullopt, std::nullopt, true);
    REQUIRE_FALSE(h.has_pending);
    REQUIRE(h.has_queued);
    REQUIRE(h.queued.user == "четыре");
    h = node.Tick(h, Query(""), Started("r4"));
    auto final = FinalText("ok четыре");
    final.request_id = "r4";
    h = node.Tick(h, Query(""), final);
    REQUIRE(h.turns.size() == 3);
    REQUIRE(h.turns.back().user == "четыре");
  }
}

namespace {

class TraceNode {
public:
  explicit TraceNode(const std::string& params = "max_turns: 4")
    : node_(params, {"turn_trace"}) {}

  TurnTrace Tick(
    const UserQuery& query,
    const AssistantResult& result,
    std::optional<ToolResult> tool = std::nullopt,
    std::optional<bool> reset = std::nullopt,
    bool cancel = false
  ) {
    history_ = node_.Tick(history_, query, result, std::move(tool), std::nullopt, reset, cancel);
    return NodeGet<TurnTrace>(node_.ctx, "turn_trace");
  }

  const DialogueHistory& History() const {
    return history_;
  }

  NodeContext& Context() {
    return node_.ctx;
  }

private:
  StateNode node_;
  DialogueHistory history_;
};

AssistantResult Hop(const std::string& id, const std::string& raw, const std::string& call, const std::string& args) {
  auto r = FinalCall(raw, call);
  r.request_id = id;
  r.tool_calls[0].id = id + "/c";
  r.tool_calls[0].arguments_json = args;
  return r;
}

ToolResult Result(const std::string& call_id, const std::string& content) {
  return ToolResult{.call_id = call_id, .name = "set", .content = content, .is_ready = true};
}

AssistantResult Answer(const std::string& id, const std::string& raw) {
  auto r = FinalText(raw);
  r.request_id = id;
  return r;
}

} // namespace

TEST_CASE("state: turn_trace follows the turn tick by tick to its final", "[assistant][node][state][trace]") {
  TraceNode node;
  REQUIRE(node.Tick(Query("turn on"), Idle()).request_id.empty());
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  auto t = node.Tick(Query(""), Started());
  REQUIRE(t.request_id == "r1");
  REQUIRE(t.user == "turn on");
  REQUIRE(t.wait_ms >= 15);
  REQUIRE(t.steps.empty());
  REQUIRE_FALSE(t.finished);

  REQUIRE(node.Tick(Query(""), Idle()).request_id.empty());

  auto first = Hop("r1", "<f_set>", "set", R"({"device_id": "lamp"})");
  first.usage = GenerationUsage{
    .context_tokens = 54,
    .reused_tokens = 40,
    .prefilled_tokens = 14,
    .generated_tokens = 8,
    .prefill_ms = 12.6,
    .generation_ms = 30.4
  };
  t = node.Tick(Query(""), first);
  REQUIRE(t.steps.size() == 1);
  const auto& round = t.steps[0];
  REQUIRE(round.usage.context_tokens == 54);
  REQUIRE(round.usage.reused_tokens == 40);
  REQUIRE(round.usage.prefilled_tokens == 14);
  REQUIRE(round.usage.generated_tokens == 8);
  REQUIRE(round.usage.prefill_ms == 12.6);
  REQUIRE(round.usage.generation_ms == 30.4);
  REQUIRE(round.ms >= 0);
  REQUIRE(round.usage.prompt_label.empty());
  REQUIRE(round.raw_text == "<f_set>");
  REQUIRE(t.steps[0].call.name == "set");
  REQUIRE(t.steps[0].call.arguments_json == R"({"device_id": "lamp"})");
  REQUIRE_FALSE(t.steps[0].has_result);

  t = node.Tick(Query(""), Idle(), Result("r1/c", R"({"status": "success"})"));
  REQUIRE(t.steps[0].result == R"({"status": "success"})");
  REQUIRE(t.steps[0].result_ms >= 0);

  auto second = Hop("r1/c", "<f_get>", "get", "{}");
  t = node.Tick(Query(""), second);
  REQUIRE(t.steps.size() == 2);
  node.Tick(Query(""), Idle(), Result("r1/c/c", R"({"status": "success", "value": 5})"));

  auto final = Answer("r1/c/c", "Done.");
  final.usage.generated_tokens = 3;
  t = node.Tick(Query(""), final);
  REQUIRE(t.steps.size() == 3);
  REQUIRE(t.steps[1].result == R"({"status": "success", "value": 5})");
  REQUIRE(t.steps[2].raw_text == "Done.");
  REQUIRE(t.steps[2].usage.generated_tokens == 3);
  REQUIRE_FALSE(t.steps[2].has_call);
  REQUIRE(ToString(t.status) == "ok");
  REQUIRE(ToString(t.finish_reason) == "eos");
  REQUIRE(t.ms >= t.wait_ms);

  REQUIRE(node.Tick(Query(""), Idle()).request_id.empty());
  REQUIRE(node.History().turns.size() == 1);
  REQUIRE(node.History().turns[0].steps.size() == 2);
}

TEST_CASE(
  "state: turn_trace takes the first of several calls in a round, as the history", "[assistant][node][state][trace]"
) {
  TraceNode node;
  node.Tick(Query("turn on"), Started());
  auto hop = Hop("r1", "<f_set><f_get>", "set", "{}");
  hop.tool_calls.push_back(ToolCall{.id = "r1/d", .name = "get", .arguments_json = "{}"});
  const auto t = node.Tick(Query(""), hop);
  REQUIRE(t.steps.size() == 1);
  REQUIRE(t.steps[0].call.name == "set");
  REQUIRE(node.History().pending.steps.size() == 1);
  REQUIRE(node.History().pending.steps[0].call.name == "set");
}

TEST_CASE("state: turn_trace keeps what the tools gave as it was", "[assistant][node][state][trace]") {
  TraceNode node;
  node.Tick(Query("turn on"), Started());
  node.Tick(Query(""), Hop("r1", "<f_set>", "set", "not json"));
  auto t = node.Tick(Query(""), Idle(), Result("r1/c", R"({"status": "fail", "error": "DEVICE_UNREACHABLE"})"));
  REQUIRE(t.steps[0].call.arguments_json == "not json");
  REQUIRE(t.steps[0].result == R"({"status": "fail", "error": "DEVICE_UNREACHABLE"})");

  auto not_ready = Result("r1/c", "{}");
  not_ready.status = AssistantStatus::ERROR;
  REQUIRE(node.Tick(Query(""), Idle(), not_ready).request_id.empty());

  TraceNode big;
  big.Tick(Query("turn on"), Started());
  std::string huge = "{\"text\": \"";
  while (huge.size() < 3000) {
    huge += "привет";
  }
  huge += "\"}";
  big.Tick(Query(""), Hop("r1", "<f_set>", "set", huge));
  t = big.Tick(Query(""), Idle(), Result("r1/c", "plain text"));
  REQUIRE(t.steps[0].result == "plain text");
  REQUIRE(t.steps[0].call.arguments_json == huge);
}

TEST_CASE("state: turn_trace of a turn that does not end well", "[assistant][node][state][trace]") {
  SECTION("declined, with the head's label") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    auto declined = Answer("r1", "");
    declined.finish_reason = StreamFinishReason::DECLINED;
    declined.usage.prompt_label = "decline";
    const auto t = node.Tick(Query(""), declined);
    REQUIRE(ToString(t.finish_reason) == "declined");
    REQUIRE(t.steps[0].usage.prompt_label == "decline");
    REQUIRE(node.History().turns.empty());
  }

  SECTION("an error after a tool call") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}"));
    node.Tick(Query(""), Idle(), Result("r1/c", "{}"));
    auto failed = Answer("r1/c", "");
    failed.status = AssistantStatus::ERROR;
    failed.error = "Repeated tool call";
    const auto t = node.Tick(Query(""), failed);
    REQUIRE(ToString(t.status) == "error");
    REQUIRE(t.error == "Repeated tool call");
    REQUIRE(t.steps.size() == 2);
  }

  SECTION("cut by the token limit") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    auto cut = Answer("r1", "Включаю све");
    cut.finish_reason = StreamFinishReason::MAX_TOKENS;
    const auto t = node.Tick(Query(""), cut);
    REQUIRE(ToString(t.status) == "ok");
    REQUIRE(ToString(t.finish_reason) == "max_tokens");
  }

  SECTION("cancelled with a tool call: the turn ends with the call that never ran") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    auto cancelled = Hop("r1", "<f_set>", "set", "{}");
    cancelled.finish_reason = StreamFinishReason::CANCELLED;
    const auto t = node.Tick(Query(""), cancelled);
    REQUIRE(ToString(t.finish_reason) == "cancelled");
    REQUIRE(t.steps[0].call.name == "set");
    REQUIRE_FALSE(t.steps[0].has_result);
    REQUIRE_FALSE(node.History().has_pending);
    REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}")).request_id.empty());
  }

  SECTION("a tool call at the token limit is a step: the turn goes on") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    auto cut = Hop("r1", "<f_set>", "set", "{}");
    cut.finish_reason = StreamFinishReason::MAX_TOKENS;
    const auto t = node.Tick(Query(""), cut);
    REQUIRE_FALSE(t.finished);
    REQUIRE(t.steps[0].call.name == "set");
    REQUIRE(node.History().has_pending);
    REQUIRE(node.History().pending.steps.size() == 1);
    REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}")).steps[0].has_result);
  }

  SECTION("with drop_truncated_reply a tool call at the token limit drops the turn") {
    TraceNode node("max_turns: 4\ndrop_truncated_reply: true");
    node.Tick(Query("q"), Started());
    auto cut = Hop("r1", "<f_set>", "set", "{}");
    cut.finish_reason = StreamFinishReason::MAX_TOKENS;
    const auto t = node.Tick(Query(""), cut);
    REQUIRE(t.finished);
    REQUIRE(t.steps[0].call.name == "set");
    REQUIRE_FALSE(node.History().has_pending);
    REQUIRE(node.History().turns.empty());
    REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}")).request_id.empty());
  }

  SECTION("cancelled: the last snapshot was the last word, a late final traces nothing") {
    TraceNode node;
    REQUIRE_FALSE(node.Tick(Query("q"), Started()).request_id.empty());
    REQUIRE(node.Tick(Query(""), Idle(), std::nullopt, std::nullopt, true).request_id.empty());
    REQUIRE(node.Tick(Query(""), Answer("r1", "late")).request_id.empty());
  }

  SECTION("a turn broken by the loop guard keeps the raw text of its last round") {
    TraceNode node;
    REQUIRE_FALSE(node.Tick(Query("q"), Started()).request_id.empty());
    auto broken = Answer("r1", "<f_set><f_get>");
    broken.status = AssistantStatus::ERROR;
    broken.error = "Expected one tool call per step";
    const auto t = node.Tick(Query(""), broken);
    REQUIRE(t.steps[0].raw_text == "<f_set><f_get>");
    REQUIRE(t.error == "Expected one tool call per step");
  }

  SECTION("a query taken in its own tick waits nothing; a query replaced in the queue is not traced") {
    TraceNode node;
    REQUIRE(node.Tick(Query("q"), Started()).wait_ms == 0);

    TraceNode queued;
    REQUIRE(queued.Tick(Query("first", "a"), Idle()).request_id.empty());
    REQUIRE(queued.Tick(Query("second", "b"), Idle()).request_id.empty());
    const auto t = queued.Tick(Query(""), Started("b"));
    REQUIRE(t.request_id == "b");
    REQUIRE(t.user == "second");
  }

  SECTION("a tool result after cancel is not traced") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}"));
    REQUIRE(node.Tick(Query(""), Idle(), std::nullopt, std::nullopt, true).request_id.empty());
    REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}")).request_id.empty());
  }

  SECTION("dialogue reset and node reset in the middle of a turn") {
    TraceNode node;
    node.Tick(Query("q"), Started());
    node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}"));
    REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}"), true).request_id.empty());
    REQUIRE(node.Tick(Query(""), Answer("r1/c", "late")).request_id.empty());

    TraceNode other;
    other.Tick(Query("q"), Started());
    NodePushEvent(other.Context(), Event{.event = ResetEvent{}});
    REQUIRE(other.Tick(Query(""), Answer("r1", "late")).request_id.empty());
    const auto t = other.Tick(Query("q2", "r2"), Started("r2"));
    REQUIRE(t.request_id == "r2");
  }

  SECTION("a new query taken by the LLM starts a new trace; the old turn's late cancel is not traced") {
    TraceNode node;
    node.Tick(Query("first"), Started());
    REQUIRE(node.Tick(Query("second", "r2"), Started("r2")).user == "second");
    auto cancelled = Answer("r1", "");
    cancelled.finish_reason = StreamFinishReason::CANCELLED;
    REQUIRE(node.Tick(Query(""), cancelled).request_id.empty());
    const auto t = node.Tick(Query(""), Answer("r2", "ok"));
    REQUIRE(t.request_id == "r2");
    REQUIRE(ToString(t.status) == "ok");
  }
}

TEST_CASE(
  "state: turn_trace of a tool result and the next round in one tick is one snapshot", "[assistant][node][state][trace]"
) {
  TraceNode node;
  node.Tick(Query("q"), Started());
  REQUIRE_FALSE(node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}")).request_id.empty());
  const auto t = node.Tick(Query(""), Answer("r1/c", "Готово."), Result("r1/c", R"({"ok": true})"));
  REQUIRE(ToString(t.status) == "ok");
  REQUIRE(t.steps.size() == 2);
  REQUIRE(t.steps[0].result == R"({"ok": true})");
  REQUIRE(t.steps[0].result_ms >= 0);
  REQUIRE(t.steps[1].raw_text == "Готово.");
  REQUIRE(node.History().turns.size() == 1);
  REQUIRE(node.Tick(Query(""), Idle()).request_id.empty());
}

TEST_CASE(
  "state: turn_trace of the next turn starts clean after a turn broken off", "[assistant][node][state][trace]"
) {
  for (const auto* way : {"cancel", "dialogue reset", "node reset"}) {
    INFO(way);
    TraceNode node;
    node.Tick(Query("first"), Started());
    REQUIRE_FALSE(node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}")).request_id.empty());
    if (std::string_view(way) == "cancel") {
      REQUIRE(node.Tick(Query(""), Idle(), std::nullopt, std::nullopt, true).request_id.empty());
    } else if (std::string_view(way) == "dialogue reset") {
      REQUIRE(node.Tick(Query(""), Idle(), std::nullopt, true).request_id.empty());
    } else {
      NodePushEvent(node.Context(), Event{.event = ResetEvent{}});
    }
    const auto t = node.Tick(Query("second", "r2"), Started("r2"));
    REQUIRE(t.request_id == "r2");
    REQUIRE(t.user == "second");
    REQUIRE(t.wait_ms == 0);
    REQUIRE(t.steps.empty());
    REQUIRE_FALSE(t.finished);
    REQUIRE(t.error.empty());
  }
}

TEST_CASE("state: turn_trace of a query in a dialogue reset tick is not started", "[assistant][node][state][trace]") {
  TraceNode node;
  REQUIRE(node.Tick(Query("first"), Started(), std::nullopt, true).request_id.empty());
  REQUIRE_FALSE(node.History().has_queued);
  const auto t = node.Tick(Query("second", "r2"), Started("r2"));
  REQUIRE(t.request_id == "r2");
  REQUIRE(t.wait_ms == 0);
}

TEST_CASE(
  "state: turn_trace of a cancel with the cancelled round in the same tick finishes the turn with it",
  "[assistant][node][state][trace]"
) {
  TraceNode node;
  node.Tick(Query("q"), Started());
  REQUIRE_FALSE(node.Tick(Query(""), Hop("r1", "<f_set>", "set", "{}")).request_id.empty());
  REQUIRE(node.Tick(Query(""), Idle(), Result("r1/c", "{}")).steps[0].has_result);
  auto cancelled = Answer("r1/c", "");
  cancelled.finish_reason = StreamFinishReason::CANCELLED;
  cancelled.usage.prefilled_tokens = 24;
  const auto t = node.Tick(Query(""), cancelled, std::nullopt, std::nullopt, true);
  REQUIRE(t.finished);
  REQUIRE(t.finish_reason == StreamFinishReason::CANCELLED);
  REQUIRE(t.steps.size() == 2);
  REQUIRE(t.steps[1].usage.prefilled_tokens == 24);
  REQUIRE_FALSE(node.History().has_pending);
  REQUIRE(node.Tick(Query(""), Idle()).request_id.empty());
}

TEST_CASE(
  "state: turn_trace of a batch has a round per call, the queued ones without a generation",
  "[assistant][node][state][trace]"
) {
  TraceNode node;
  node.Tick(Query("turn on both"), Idle());
  node.Tick(Query(""), Started());
  auto first = Hop("r1", "<f_set>a", "set", R"({"device_id": "a"})");
  first.queued_calls = 1;
  first.usage.generated_tokens = 9;
  REQUIRE(node.Tick(Query(""), first).steps.size() == 1);
  // The next call of the batch comes in the tick of the first one's result.
  auto t = node.Tick(Query(""), Hop("r1/c", "<f_set>b", "set", R"({"device_id": "b"})"), Result("r1/c", "{}"));
  REQUIRE(t.steps.size() == 2);
  REQUIRE(t.steps[0].result == "{}");
  REQUIRE(t.steps[1].usage.generated_tokens == 0);
  REQUIRE(t.steps[1].call.arguments_json == R"({"device_id": "b"})");
  node.Tick(Query(""), Idle(), Result("r1/c/c", R"({"status": "fail"})"));
  t = node.Tick(Query(""), Answer("r1/c/c", "Done."));
  REQUIRE(t.finished);
  REQUIRE(t.steps.size() == 3);
  REQUIRE(t.steps[0].usage.generated_tokens == 9);
  REQUIRE(t.steps[1].result == R"({"status": "fail"})");
  REQUIRE(node.History().turns.at(0).steps.size() == 2);
  REQUIRE(node.History().turns.at(0).steps[0].queued_calls == 1);
}

TEST_CASE("state: without turn_trace connected the history is the same", "[assistant][node][state][trace]") {
  StateNode plain("max_turns: 4");
  StateNode traced("max_turns: 4", {"turn_trace"});
  DialogueHistory a;
  DialogueHistory b;
  const std::vector<std::pair<UserQuery, AssistantResult>> ticks = {
    {Query("turn on"), Started()}, {Query(""), FinalText("Done.")}
  };
  for (const auto& [query, result] : ticks) {
    a = plain.Tick(a, query, result);
    b = traced.Tick(b, query, result);
    NodeGet<TurnTrace>(traced.ctx, "turn_trace");
  }
  REQUIRE(a.turns.size() == 1);
  REQUIRE(b.turns.size() == 1);
  REQUIRE(a.turns[0].assistant == b.turns[0].assistant);
}

TEST_CASE("state: a batch of calls becomes call-result pairs, one per call", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("turn on both"), Started());
  // Batch calls come from the guard one at a time: the next one in the tick of the previous one's result.
  auto first = FinalCall("<start_function_call><f_set>a", "set");
  first.queued_calls = 1;
  h = node.Tick(h, Query(""), first);
  REQUIRE(h.pending.steps.size() == 1);
  REQUIRE(h.pending.steps[0].queued_calls == 1);

  auto second = FinalCall("<start_function_call><f_set>b", "set");
  second.request_id = "c1";
  second.tool_calls[0].id = "c2";
  h = node.Tick(
    h, Query(""), second, ToolResult{.call_id = "c1", .content = "{\"status\": \"success\"}", .is_ready = true}
  );
  REQUIRE(h.pending.steps.size() == 2);
  REQUIRE(h.pending.steps[0].has_result);
  REQUIRE(h.pending.steps[1].assistant_raw == "<start_function_call><f_set>b");
  REQUIRE(h.pending.steps[1].queued_calls == 0);
  REQUIRE_FALSE(h.pending.steps[1].has_result);

  h = node.Tick(
    h, Query(""), Idle(), ToolResult{.call_id = "c2", .content = "{\"status\": \"fail\"}", .is_ready = true}
  );
  auto final = FinalText("Done.");
  final.request_id = "c2";
  h = node.Tick(h, Query(""), final);
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].steps.size() == 2);
  REQUIRE(h.turns[0].steps[1].result.content == "{\"status\": \"fail\"}");
}

TEST_CASE("state: the history takes the escaped answer, or the raw one without it", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto h = node.Tick(DialogueHistory{}, Query("q"), Started());
  auto call = FinalCall("<f_set>a", "set");
  call.history_text = "<f_set>a\xE2\x80\x8B";
  h = node.Tick(h, Query(""), call);
  REQUIRE(h.pending.steps.size() == 1);
  REQUIRE(h.pending.steps[0].assistant_raw == "<f_set>a\xE2\x80\x8B");
  auto answer = FinalText("raw answer");
  answer.request_id = "c1";
  h = node.Tick(h, Query(""), answer, ToolResult{.call_id = "c1", .content = "{}", .is_ready = true});
  REQUIRE(h.turns.size() == 1);
  REQUIRE(h.turns[0].assistant == "raw answer");
}

TEST_CASE("state: a reply cut by the token limit stays in the history by default", "[assistant][node][state]") {
  StateNode node("max_turns: 4");
  auto history = node.Tick({}, Query("q"), Started());
  auto cut = FinalText("cut answer");
  cut.finish_reason = StreamFinishReason::MAX_TOKENS;
  history = node.Tick(history, Query(""), cut);
  REQUIRE_FALSE(history.has_pending);
  REQUIRE(history.turns.size() == 1);
  REQUIRE(history.turns[0].assistant == "cut answer");
}

TEST_CASE(
  "state: with drop_truncated_reply the token limit drops the whole turn just like an error, including completed tools",
  "[assistant][node][state]"
) {
  const bool after_tool = GENERATE(false, true);
  const bool error = GENERATE(false, true);
  CAPTURE(after_tool, error);
  StateNode node("max_turns: 4\ndrop_truncated_reply: true");
  auto history = node.Tick({}, Query("previous"), Started());
  history = node.Tick(history, Query(""), FinalText("previous answer"));
  history = node.Tick(history, Query("discarded query", "r2"), Started("r2"));
  if (after_tool) {
    auto call = FinalCall("<f_set>", "set");
    call.request_id = "r2";
    history = node.Tick(history, Query(""), call);
    history = node.Tick(
      history,
      Query(""),
      Idle(),
      ToolResult{.call_id = "c1", .name = "set", .content = "{\"status\": \"success\"}", .is_ready = true}
    );
    REQUIRE(history.pending.steps[0].has_result);
  }
  auto cut = FinalText("truncated 35.6899999999");
  cut.request_id = after_tool ? "c1" : "r2";
  cut.status = error ? AssistantStatus::ERROR : AssistantStatus::OK;
  cut.finish_reason = error ? StreamFinishReason::ERROR : StreamFinishReason::MAX_TOKENS;
  history = node.Tick(history, Query(""), cut);
  REQUIRE_FALSE(history.has_pending);
  REQUIRE_FALSE(history.has_queued);
  REQUIRE(history.pending.user.empty());
  REQUIRE(history.pending.steps.empty());
  REQUIRE(history.turns.size() == 1);
  REQUIRE(history.turns[0].user == "previous");
  REQUIRE(history.turns[0].assistant == "previous answer");

  auto builder = CreateNode("dialogue_request_builder", "", {}, {"dialogue_history"});
  NodePush(builder, "dialogue_history", history, true, 0);
  NodePush(builder, "user_query", Query("next", "r3"));
  const auto request = NodeGet<AssistantRequest>(builder, "assistant_request");
  PromptTemplateParams params;
  params.user = {"user:", "\n"};
  params.assistant = {"assistant:", "\n"};
  params.generation_prefix = "assistant:";
  REQUIRE(PromptTemplate(params).Render(request) == "user:previous\nassistant:previous answer\nuser:next\nassistant:");
  auto final = FinalText("next answer");
  final.request_id = "r3";
  history = node.Tick(history, Query("next", "r3"), final);
  REQUIRE(history.turns.size() == 2);
  REQUIRE(history.turns.back().user == "next");
  REQUIRE(history.turns.back().assistant == "next answer");
}
