#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

UserQuery Query(const std::string& text, const std::string& id = "r1") {
  UserQuery q;
  q.request_id = text.empty() ? "" : id;
  q.text = text;
  q.status = text.empty() ? AssistantStatus::SKIPPED : AssistantStatus::OK;
  return q;
}

SceneSnapshot Scene(const std::string& text, const std::string& version) {
  return SceneSnapshot{.text = text, .version = version, .allow_empty = false};
}

NodeContext Builder(const std::string& params, std::unordered_set<std::string> optional_inputs) {
  return CreateNode("dialogue_request_builder", params, {}, std::move(optional_inputs));
}

} // namespace

TEST_CASE("builder: idle tick gives an idle request", "[assistant][node][builder]") {
  auto ctx = Builder("system_prompt: sys", {});
  NodePush(ctx, "user_query", Query(""));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::NONE);
  REQUIRE(request.messages.empty());
}

TEST_CASE("builder: plain query renders system and user", "[assistant][node][builder]") {
  auto ctx = Builder("system_prompt: sys\ntemperature: 0.5\nmax_tokens: 7", {});
  NodePush(ctx, "user_query", Query("hello"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.request_id == "r1");
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[0].role == MessageRole::SYSTEM);
  REQUIRE(request.messages[0].content == "sys");
  REQUIRE(request.messages[1].role == MessageRole::USER);
  REQUIRE(request.messages[1].content == "hello");
  REQUIRE(request.generation.temperature.has_value());
  REQUIRE(*request.generation.temperature == Approx(0.5f));
  REQUIRE(request.generation.max_tokens == 7);
}

TEST_CASE("builder: temperature stays unset unless configured", "[assistant][node][builder]") {
  auto ctx = Builder("", {});
  NodePush(ctx, "user_query", Query("hello"));
  REQUIRE_FALSE(NodeGet<AssistantRequest>(ctx, "assistant_request").generation.temperature.has_value());
}

TEST_CASE("builder: a query of a follow-up session makes a follow-up request", "[assistant][node][builder]") {
  auto plain = Builder("system_prompt: sys", {});
  NodePush(plain, "user_query", Query("hello"));
  REQUIRE_FALSE(NodeGet<AssistantRequest>(plain, "assistant_request").follow_up);

  auto ctx = Builder("system_prompt: sys", {"follow_up"});
  NodePush(ctx, "follow_up", true, true, 0);
  NodePush(ctx, "user_query", Query("в спальне"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.follow_up);

  NodePush(ctx, "follow_up", false, true, 0);
  NodePush(ctx, "user_query", Query("включи свет", "r2"));
  REQUIRE_FALSE(NodeGet<AssistantRequest>(ctx, "assistant_request").follow_up);
}

TEST_CASE("builder: host system prompt overrides the param", "[assistant][node][builder]") {
  auto ctx = Builder("system_prompt: sys", {"system_prompt"});
  NodePush<std::string>(ctx, "system_prompt", "host rules", true, 0);
  NodePush(ctx, "user_query", Query("hello"));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").messages[0].content == "host rules");
}

TEST_CASE("builder: scene warms the runner until the state has seen that version", "[assistant][node][builder]") {
  auto ctx = Builder("", {"scene_snapshot", "dialogue_history"});
  DialogueHistory seen;

  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.request_id == "warm:v1");
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[1].role == MessageRole::SCENE);
  REQUIRE(request.messages[1].content == "lamp");

  seen.last_scene_version = "v1";
  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);

  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
  NodePush(ctx, "user_query", Query("turn on"));
  request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.messages.size() == 3);
  REQUIRE(request.messages[1].role == MessageRole::SCENE);
  REQUIRE(request.messages[2].content == "turn on");

  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\nfan\n", "v2"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::WARM);

  seen.has_pending = true;
  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\nfan\n", "v2"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);
}

TEST_CASE("builder: a coming query warms system, scene and history", "[assistant][node][builder]") {
  auto ctx = Builder("", {"scene_snapshot", "dialogue_history", "warm"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  const auto run = [&](bool warm, const std::string& text = "") {
    NodePush(ctx, "dialogue_history", seen, true, 0);
    NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
    NodePush(ctx, "warm", warm, true, 0);
    NodePush(ctx, "user_query", Query(text));
    return NodeGet<AssistantRequest>(ctx, "assistant_request");
  };

  REQUIRE(run(false).kind == AssistantRequestKind::NONE);

  auto request = run(true);
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.request_id == "warm:v1");
  REQUIRE(request.messages.size() == 2);

  DialogueTurn turn;
  turn.request_id = "r7";
  turn.user = "turn on";
  turn.assistant = "Done.";
  turn.completed = true;
  seen.turns.push_back(turn);
  request = run(true);
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.request_id == "warm:v1:1:r7");
  REQUIRE(request.messages.size() == 4);
  REQUIRE(request.messages[2].role == MessageRole::USER);
  REQUIRE(request.messages[2].content == "turn on");
  REQUIRE(request.messages[3].role == MessageRole::ASSISTANT);

  // A query in the same tick is an ordinary generation; with a turn in work there is no warm-up.
  REQUIRE(run(true, "turn off").kind == AssistantRequestKind::GENERATE);
  seen.has_pending = true;
  REQUIRE(run(true).kind == AssistantRequestKind::NONE);
}

TEST_CASE("builder: a coming query with recognized-so-far text warms the history once", "[assistant][node][builder]") {
  auto ctx = Builder("", {"scene_snapshot", "dialogue_history", "warm", "partial_text"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  DialogueTurn turn;
  turn.request_id = "r7";
  turn.user = "turn on";
  turn.assistant = "Done.";
  turn.completed = true;
  seen.turns.push_back(turn);
  const auto run = [&](bool warm, const std::string& partial) {
    NodePush(ctx, "dialogue_history", seen, true, 0);
    NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
    NodePush(ctx, "warm", warm, true, 0);
    NodePush(ctx, "partial_text", partial, true, 0);
    NodePush(ctx, "user_query", Query(""));
    return NodeGet<AssistantRequest>(ctx, "assistant_request");
  };

  // A session start and the already recognized beginning of the query in one tick: the history once, then the
  // beginning of the query.
  auto request = run(true, "turn off the");
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.request_id.rfind("prewarm:", 0) == 0);
  REQUIRE(request.messages.size() == 5);
  REQUIRE(request.messages[1].role == MessageRole::SCENE);
  REQUIRE(request.messages[2].content == "turn on");
  REQUIRE(request.messages[3].role == MessageRole::ASSISTANT);
  REQUIRE(request.messages[4].role == MessageRole::USER);
  REQUIRE(request.messages[4].content == "turn off the");

  // The same text is already warmed: the session start signal warms only the history.
  request = run(true, "turn off the");
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.request_id == "warm:v1:1:r7");
  REQUIRE(request.messages.size() == 4);
  REQUIRE(run(false, "turn off the").kind == AssistantRequestKind::NONE);
}

TEST_CASE("builder: empty scene that is not allowed is skipped", "[assistant][node][builder]") {
  auto ctx = Builder("", {"scene_snapshot", "dialogue_history"});
  NodePush(ctx, "dialogue_history", DialogueHistory{}, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("", "v0"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);
  NodePush(ctx, "dialogue_history", DialogueHistory{}, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("", "v0"), true, 0);
  NodePush(ctx, "user_query", Query("hi"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[1].role == MessageRole::USER);
}

TEST_CASE("builder: history window and tool steps are replayed", "[assistant][node][builder]") {
  auto ctx = Builder("max_history_turns: 1", {"dialogue_history"});
  DialogueHistory history;
  DialogueTurn old;
  old.user = "old";
  old.assistant = "old answer";
  old.completed = true;
  DialogueTurn last;
  last.user = "read";
  ToolStep step;
  step.assistant_raw = "<call>";
  step.call = ToolCall{.name = "get", .arguments_json = "{}"};
  step.has_result = true;
  step.result = ToolResult{.call_id = "c1", .name = "get", .content = "{\"v\": 1}", .is_ready = true};
  last.steps.push_back(step);
  last.assistant = "it is one";
  last.completed = true;
  history.turns = {old, last};

  NodePush(ctx, "dialogue_history", history, true, 0);
  NodePush(ctx, "user_query", Query("next", "r2"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.messages.size() == 6);
  REQUIRE(request.messages[1].content == "read");
  REQUIRE(request.messages[2].role == MessageRole::ASSISTANT);
  REQUIRE(request.messages[2].content == "<call>");
  REQUIRE(request.messages[2].tool_calls.size() == 1);
  REQUIRE(request.messages[3].role == MessageRole::TOOL);
  REQUIRE(request.messages[3].content == "{\"v\": 1}");
  REQUIRE(request.messages[3].tool_call_id == "c1");
  REQUIRE(request.messages[4].content == "it is one");
  REQUIRE(request.messages[5].content == "next");
}

TEST_CASE("builder: a ready tool result renders the pending hop", "[assistant][node][builder]") {
  auto ctx = Builder("", {"dialogue_history", "scene_snapshot", "tool_result"});
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "r1";
  history.pending.user = "turn on";
  history.pending.scene_text = "scene at start";
  history.pending.scene_version = "v1";
  ToolStep step;
  step.assistant_raw = "<f_set>";
  step.call = ToolCall{.id = "call-1", .name = "set", .arguments_json = "{}"};
  history.pending.steps.push_back(step);

  NodePush(ctx, "dialogue_history", history, true, 0);
  auto current_scene = Scene("scene changed meanwhile", "v2");
  SECTION("scene changed") {}
  SECTION("scene became invalid") {
    current_scene.status = AssistantStatus::ERROR;
    current_scene.error = "scene too large";
  }
  NodePush(ctx, "scene_snapshot", current_scene, true, 0);
  NodePush(
    ctx,
    "tool_result",
    ToolResult{.call_id = "call-1", .name = "set", .content = "{\"status\": \"success\"}", .is_ready = true},
    true,
    0
  );
  NodePush(ctx, "user_query", Query(""));

  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.status == AssistantStatus::OK);
  REQUIRE(request.request_id == "call-1");
  REQUIRE(request.messages.size() == 5);
  REQUIRE(request.messages[1].role == MessageRole::SCENE);
  REQUIRE(request.messages[1].content == "scene at start");
  REQUIRE(request.messages[2].content == "turn on");
  REQUIRE(request.messages[3].content == "<f_set>");
  REQUIRE(request.messages[4].role == MessageRole::TOOL);
  REQUIRE(request.messages[4].content == "{\"status\": \"success\"}");
}

TEST_CASE("builder: a new query on the same tick wins over the pending hop", "[assistant][node][builder]") {
  auto ctx = Builder("", {"dialogue_history", "tool_result"});
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "r1";
  history.pending.user = "turn on";
  ToolStep step;
  step.assistant_raw = "<f_set>";
  step.call = ToolCall{.name = "set", .arguments_json = "{}"};
  history.pending.steps.push_back(step);

  NodePush(ctx, "dialogue_history", history, true, 0);
  NodePush(
    ctx, "tool_result", ToolResult{.call_id = "call-1", .name = "set", .content = "ok", .is_ready = true}, true, 0
  );
  NodePush(ctx, "user_query", Query("turn off", "r2"));

  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.request_id == "r2");
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[1].role == MessageRole::USER);
  REQUIRE(request.messages[1].content == "turn off");
}

TEST_CASE("builder: tools and seed history are folded in", "[assistant][node][builder]") {
  auto ctx = Builder("", {"tool_specs", "seed_history"});
  ToolSpecs specs;
  specs.tools.push_back(ToolSpec{"ping", "check", ""});
  DialogueHistory seed;
  DialogueTurn turn;
  turn.user = "seed q";
  turn.assistant = "seed a";
  turn.completed = true;
  seed.turns.push_back(turn);
  NodePush(ctx, "tool_specs", specs, true, 0);
  NodePush(ctx, "seed_history", seed, true, 0);
  NodePush(ctx, "user_query", Query("q"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.tools.size() == 1);
  REQUIRE(request.messages.size() == 4);
  REQUIRE(request.messages[1].content == "seed q");
  REQUIRE(request.messages[2].content == "seed a");
}

TEST_CASE("builder: user prefix and suffix wrap the query text", "[assistant][node][builder]") {
  auto ctx = Builder("user_prefix: \"data:\"\nuser_suffix: \"!\"", {"dialogue_history"});
  DialogueHistory history;
  history.turns.push_back(DialogueTurn{.user = "old", .assistant = "a", .completed = true});
  NodePush(ctx, "dialogue_history", history, true, 0);
  NodePush(ctx, "user_query", Query("q"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.messages[1].content == "data:old!");
  REQUIRE(request.messages[3].content == "data:q!");
}

TEST_CASE("builder: upstream query error becomes an error request", "[assistant][node][builder]") {
  auto ctx = Builder("", {});
  UserQuery bad;
  bad.request_id = "e";
  bad.status = AssistantStatus::ERROR;
  bad.error = "asr failed";
  NodePush(ctx, "user_query", bad);
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.status == AssistantStatus::ERROR);
  REQUIRE(request.error == "asr failed");
  REQUIRE(request.request_id == "e");
}

TEST_CASE("builder: scene failures only reject new queries that use the scene", "[assistant][node][builder]") {
  const bool include_scene = GENERATE(false, true);
  auto ctx = Builder(include_scene ? "" : "include_scene: false", {"scene_snapshot", "dialogue_history"});
  SceneSnapshot scene{.text = "bad", .version = "v2", .status = AssistantStatus::ERROR, .error = "scene failed"};
  auto tick = [&](UserQuery query) {
    NodePush(ctx, "scene_snapshot", scene, true, 0);
    NodePush(ctx, "dialogue_history", DialogueHistory{}, true, 0);
    NodePush(ctx, "user_query", query);
    return NodeGet<AssistantRequest>(ctx, "assistant_request");
  };
  const auto idle = tick({});
  REQUIRE(idle.kind == AssistantRequestKind::NONE);
  REQUIRE(idle.status == AssistantStatus::OK);
  const auto request = tick(Query("new", "r2"));
  REQUIRE(request.request_id == "r2");
  REQUIRE(request.status == (include_scene ? AssistantStatus::ERROR : AssistantStatus::OK));
}

TEST_CASE("builder: a new system prompt warms the runner once", "[assistant][node][builder]") {
  auto ctx = Builder("warm_on_system_change: true", {"scene_snapshot", "dialogue_history", "system_prompt"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  const auto run = [&](const std::string& system, const std::string& text = "") {
    NodePush(ctx, "dialogue_history", seen, true, 0);
    NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
    NodePush(ctx, "system_prompt", system, true, 0);
    NodePush(ctx, "user_query", Query(text));
    return NodeGet<AssistantRequest>(ctx, "assistant_request");
  };

  // An empty system prompt is already in the runtime: nothing to warm, the id is the same as without a change.
  REQUIRE(run("").kind == AssistantRequestKind::NONE);

  auto male = run("Ты — мужской персонаж.");
  REQUIRE(male.kind == AssistantRequestKind::WARM);
  REQUIRE(male.messages.size() == 2);
  REQUIRE(male.messages[0].content == "Ты — мужской персонаж.");
  REQUIRE(male.messages[1].content == "lamp");
  REQUIRE(male.request_id.rfind("warm:v1#", 0) == 0);
  REQUIRE(run("Ты — мужской персонаж.").kind == AssistantRequestKind::NONE);

  auto female = run("Ты — женский персонаж.");
  REQUIRE(female.kind == AssistantRequestKind::WARM);
  REQUIRE(female.request_id != male.request_id);

  // A query with a new prompt warms it by itself: after it the prompt is already in the runtime.
  REQUIRE(run("Ты — мужской персонаж.", "привет").kind == AssistantRequestKind::GENERATE);
  REQUIRE(run("Ты — мужской персонаж.").kind == AssistantRequestKind::NONE);

  // Going back to the previous prompt is a warm-up too, with the previous id.
  auto back = run("");
  REQUIRE(back.kind == AssistantRequestKind::WARM);
  REQUIRE(back.request_id == "warm:v1");
  REQUIRE(back.messages[0].content.empty());
}

TEST_CASE("builder: a new system prompt waits for the turn in work", "[assistant][node][builder]") {
  auto ctx = Builder("warm_on_system_change: true", {"scene_snapshot", "dialogue_history", "system_prompt"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  seen.has_pending = true;
  const auto run = [&](const std::string& system) {
    NodePush(ctx, "dialogue_history", seen, true, 0);
    NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
    NodePush(ctx, "system_prompt", system, true, 0);
    NodePush(ctx, "user_query", Query(""));
    return NodeGet<AssistantRequest>(ctx, "assistant_request");
  };
  REQUIRE(run("Ты — женский персонаж.").kind == AssistantRequestKind::NONE);
  seen.has_pending = false;
  REQUIRE(run("Ты — женский персонаж.").kind == AssistantRequestKind::WARM);
}

TEST_CASE("builder: a new system prompt with the history warm keeps the history", "[assistant][node][builder]") {
  auto ctx = Builder("warm_on_system_change: true", {"scene_snapshot", "dialogue_history", "system_prompt", "warm"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  DialogueTurn turn;
  turn.request_id = "r7";
  turn.user = "turn on";
  turn.assistant = "Done.";
  turn.completed = true;
  seen.turns.push_back(turn);
  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
  NodePush(ctx, "system_prompt", std::string("Ты — женский персонаж."), true, 0);
  NodePush(ctx, "warm", true, true, 0);
  NodePush(ctx, "user_query", Query(""));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::WARM);
  REQUIRE(request.messages.size() == 4);
  REQUIRE(request.messages[0].content == "Ты — женский персонаж.");
  REQUIRE(request.request_id.size() > std::string("warm:v1:1:r7").size());
  REQUIRE(request.request_id.substr(request.request_id.size() - 5) == ":1:r7");
}

TEST_CASE("builder: without the option a new system prompt does not warm", "[assistant][node][builder]") {
  auto ctx = Builder("", {"scene_snapshot", "dialogue_history", "system_prompt"});
  DialogueHistory seen;
  seen.last_scene_version = "v1";
  NodePush(ctx, "dialogue_history", seen, true, 0);
  NodePush(ctx, "scene_snapshot", Scene("lamp\n", "v1"), true, 0);
  NodePush(ctx, "system_prompt", std::string("Ты — женский персонаж."), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);
}

TEST_CASE(
  "builder: a batch generates after the result of its last call, history unrolled", "[assistant][node][builder]"
) {
  auto ctx = Builder("user_prefix: \"<sep>\"", {"dialogue_history", "tool_result"});
  DialogueHistory history;
  history.has_pending = true;
  history.pending.request_id = "q";
  history.pending.user = "выключи всё";
  const std::string call1
    = "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>false";
  const std::string call2
    = "<start_function_call><f_set_device_parameter><d_light>1<arg_sep><p_rw_on_off><arg_sep>false";
  ToolStep step;
  step.assistant_raw = call1;
  step.call = ToolCall{.id = "q/tool/1", .name = "set_device_parameter"};
  step.queued_calls = 1;
  history.pending.steps.push_back(step);
  const auto success = [](const std::string& id) {
    return ToolResult{
      .call_id = id, .name = "set_device_parameter", .content = "{\"status\": \"success\"}", .is_ready = true
    };
  };

  NodePush(ctx, "dialogue_history", history, true, 0);
  NodePush(ctx, "tool_result", success("q/tool/1"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);

  history.pending.steps[0].result = success("q/tool/1");
  history.pending.steps[0].has_result = true;
  step.assistant_raw = call2;
  step.call.id = "q/tool/2";
  step.queued_calls = 0;
  history.pending.steps.push_back(step);
  NodePush(ctx, "dialogue_history", history, true, 0);
  NodePush(ctx, "tool_result", success("q/tool/2"), true, 0);
  NodePush(ctx, "user_query", Query(""));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.kind == AssistantRequestKind::GENERATE);
  REQUIRE(request.request_id == "q/tool/2");

  // Roles and separators are the same as for single calls: an assistant + function results pair per call.
  const PromptTemplate compact(ParsePromptTemplateParams(YAML::Load(R"(
    system: {prefix: "system<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    scene: {prefix: "", suffix: ""}
    user: {prefix: "user<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    assistant: {prefix: "assistant<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    tool: {prefix: "function results<|role_sep|>\n", suffix: "<|message_sep|>\n\n"}
    generation_prefix: "assistant<|role_sep|>\n"
    render_empty_system: true
  )")));
  const std::string result = "function results<|role_sep|>\n{\"status\": \"success\"}<|message_sep|>\n\n";
  REQUIRE(
    compact.Render(request)
    == "system<|role_sep|>\n<|message_sep|>\n\nuser<|role_sep|>\n<sep>выключи всё<|message_sep|>\n\n"
       "assistant<|role_sep|>\n" + call1 + "<|message_sep|>\n\n" + result + "assistant<|role_sep|>\n" + call2
       + "<|message_sep|>\n\n" + result + "assistant<|role_sep|>\n"
  );
}

TEST_CASE(
  "demo prompt keeps one scene separator across turns and history trimming", "[assistant][node][builder][demo]"
) {
  const auto config = YAML::LoadFile(
    std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/giga_edge/modules/smart_home_ha/tests/data/function_calling/config.yaml"
  );
  const auto params = [&](const std::string& name) {
    for (const auto& node : config["nodes"]) {
      if (node["name"].as<std::string>() == name) {
        return node["params"];
      }
    }
    throw std::runtime_error("Missing demo node: " + name);
  };
  const auto template_params = ParsePromptTemplateParams(params("llm")["template"]);
  const PromptTemplate current(template_params);
  auto old_template = template_params;
  old_template.scene.suffix.clear();
  auto old_params = YAML::Clone(params("request"));
  old_params["user_prefix"] = "<sep>";
  auto builder = Builder(YAML::Dump(params("request")), {"scene_snapshot", "dialogue_history"});
  auto old_builder = Builder(YAML::Dump(old_params), {"scene_snapshot", "dialogue_history"});
  auto state = CreateNode("dialogue_state_update", YAML::Dump(params("history")));
  DialogueHistory history;
  const std::string scene = "<d_light>1 Лампа [Кухня]<p_rw_on_off>";
  const auto render = [&](NodeContext& node, const PromptTemplate& tpl, const UserQuery& query) {
    NodePush(node, "scene_snapshot", Scene(scene, "v1"), true, 0);
    NodePush(node, "dialogue_history", history, true, 0);
    NodePush(node, "user_query", query);
    return tpl.Render(NodeGet<AssistantRequest>(node, "assistant_request"));
  };
  const auto warm = render(builder, current, Query(""));
  const auto old_warm = render(old_builder, PromptTemplate(old_template), Query(""));
  const auto first = render(builder, current, Query("q0"));
  REQUIRE(first == render(old_builder, PromptTemplate(old_template), Query("q0")));
  REQUIRE(first.compare(0, warm.size(), warm) == 0);
  REQUIRE(first.compare(0, old_warm.size(), old_warm) == 0);
  REQUIRE(warm == old_warm + "<sep>");

  const std::string start = "system<|role_sep|>\n<|message_sep|>\n\nuser<|role_sep|>\n";
  const std::string stop = "<|message_sep|>\n\n";
  for (int i = 0; i < 5; ++i) {
    const auto query = Query("q" + std::to_string(i), "r" + std::to_string(i));
    const auto prompt = render(builder, current, query);
    std::string expected = start + scene + "<sep>";
    for (int j = 0; j < i; ++j) {
      expected += "q" + std::to_string(j) + stop + "assistant<|role_sep|>\na" + std::to_string(j) + stop
        + "user<|role_sep|>\n";
    }
    expected += query.text + stop + "assistant<|role_sep|>\n";
    REQUIRE(prompt == expected);
    NodePush(state, "previous_history", history, true, 0);
    NodePush(state, "user_query", query, true, 0);
    AssistantResult result;
    result.request_id = query.request_id;
    result.is_final = true;
    result.raw_text = "a" + std::to_string(i);
    result.finish_reason = StreamFinishReason::EOS;
    NodePush(state, "assistant_result", result);
    history = NodeGet<DialogueHistory>(state, "dialogue_history");
  }
  REQUIRE(history.turns.size() == 2);
  REQUIRE(
    render(builder, current, Query("q5"))
    == start + scene + "<sep>q3" + stop + "assistant<|role_sep|>\na3" + stop + "user<|role_sep|>\nq4" + stop
      + "assistant<|role_sep|>\na4" + stop + "user<|role_sep|>\nq5" + stop + "assistant<|role_sep|>\n"
  );
}
