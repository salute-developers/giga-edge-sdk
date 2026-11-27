#include "test_config.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/function_calling/nodes/fc_tool_result/node.h"
#include "giga_edge/modules/smart_home_ha/nodes.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {

constexpr auto kFunctionCalling = "giga_edge/modules/smart_home_ha/tests/data/function_calling/config.yaml";
constexpr auto kOfflineAssistant = "giga_edge/modules/smart_home_ha/tests/data/offline_assistant/config.yaml";

YAML::Node DemoParams(const std::string& graph, const std::string& name) {
  const auto config = YAML::LoadFile(std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/" + graph);
  for (const auto& node : config["nodes"]) {
    if (node["name"].as<std::string>() == name) {
      return node["params"];
    }
  }
  throw std::runtime_error("Missing demo node: " + name);
}

AssistantResult Generation(const std::string& request_id, const std::string& parameter, Json::Value value) {
  auto result = Action("set_device_parameter", parameter, std::move(value));
  result.request_id = request_id;
  auto& call = result.tool_calls[0];
  call.id = request_id + "/tool/1";
  call.raw_text = "<f_set_device_parameter><d_light>0<arg_sep><p_rw_" + parameter + ">";
  result.raw_text = call.raw_text;
  result.history_text = call.raw_text;
  return result;
}

// The nodes of the demo graphs from the LLM result to the session reply, with the params of the graphs; the LLM and
// MCP are played by the test, a completion of MCP comes in the tick after its action.
struct DemoTurn {
  explicit DemoTurn(const YAML::Node& guard_params)
    : guard(CreateNode("tool_loop_guard", guard_params, {}, {"tool_result", "cancel", "dialogue_reset"}))
    , resolver(CreateNode("ha_action_resolver", ""))
    , format(CreateNode("fc_tool_result", DemoParams(kFunctionCalling, "fc_tool_result")))
    , history(CreateNode(
        "dialogue_state_update",
        DemoParams(kFunctionCalling, "history"),
        {},
        {"tool_result", "scene_snapshot", "dialogue_reset", "cancel"}
      ))
    , request(CreateNode(
        "dialogue_request_builder", DemoParams(kFunctionCalling, "request"), {}, {"scene_snapshot", "dialogue_history"}
      ))
    , session(CreateNode("assistant_session", DemoParams(kOfflineAssistant, "assistant_session"))) {}

  void Tick(const UserQuery& query, const AssistantResult& generation) {
    CompactToolResults results;
    if (!completions.empty()) {
      for (const auto& completion : completions) {
        const auto done = ha::Complete(completion);
        results.insert(results.end(), done.results.begin(), done.results.end());
      }
      completions.clear();
    }
    NodePush(format, "tool_results", results);
    const auto tool_result = NodeGet<ToolResult>(format, "tool_result");

    NodePush(request, "scene_snapshot", scene, true, 0);
    NodePush(request, "dialogue_history", dialogue, true, 0);
    NodePush(request, "user_query", query);
    prompt = NodeGet<AssistantRequest>(request, "assistant_request");

    NodePush(guard, "dialogue_reset", false, true, 0);
    NodePush(guard, "cancel", false, true, 0);
    NodePush(guard, "tool_result", tool_result, true, 0);
    NodePush(guard, "user_query", query, true, 0);
    NodePush(guard, "assistant_result", generation);
    result = NodeGet<AssistantResult>(guard, "assistant_result");

    NodePush(resolver, "user_query", query);
    NodePush(resolver, "assistant_result", result);
    NodePush(resolver, "ha_catalog", Catalog());
    NodePush(resolver, "continuations", std::vector<McpResolvedAction>{});
    NodePush(resolver, "dialogue_reset", false);
    action = NodeGet<McpResolvedAction>(resolver, "mcp_resolved_action");
    if (action.status == AssistantStatus::ERROR) {
      completions.push_back({.action = action, .is_error = true, .error = action.error});
    }

    NodePush(history, "previous_history", dialogue, true, 0);
    NodePush(history, "user_query", query, true, 0);
    NodePush(history, "tool_result", tool_result, true, 0);
    NodePush(history, "scene_snapshot", scene, true, 0);
    NodePush(history, "dialogue_reset", false, true, 0);
    NodePush(history, "cancel", false, true, 0);
    NodePush(history, "assistant_result", result);
    dialogue = NodeGet<DialogueHistory>(history, "dialogue_history");

    NodePush(session, "session_start", !query.text.empty(), true, 0);
    NodePush(session, "manual_activation", false, true, 0);
    NodePush(session, "session_finish", false, true, 0);
    NodePush(session, "query_text", query.text, true, 0);
    NodePush(session, "assistant_result", result);
    events = NodeGet<AssistantSessionEvents>(session, "events");
  }

  NodeContext guard, resolver, format, history, request, session;
  SceneSnapshot scene{.text = "<d_light>0 Bedroom lamp [Bedroom]<p_rw_on_off><p_rw_light_brightness>", .version = "v1"};
  std::vector<McpActionCompletion> completions;
  DialogueHistory dialogue;
  AssistantRequest prompt;
  AssistantResult result;
  McpResolvedAction action;
  AssistantSessionEvents events;
};

} // namespace

TEST_CASE("HA demo graph answers a repeated failed call by its error code and goes on", "[ha][demo][refusal]") {
  const bool configured = GENERATE(true, false);
  auto guard_params = YAML::Clone(DemoParams(kFunctionCalling, "guard"));
  if (!configured) {
    guard_params.remove("failed_call_reply");
    guard_params.remove("failed_call_replies");
  }
  DemoTurn turn(guard_params);
  const std::string refusal = "Это устройство так не умеет.";
  REQUIRE(
    DemoParams(kFunctionCalling, "guard")["failed_call_replies"]["UNKNOWN_PARAMETER"].as<std::string>() == refusal
  );

  // The lamp has no modes: the adapter fails the call with UNKNOWN_PARAMETER.
  const UserQuery mode{.request_id = "fc-request-1", .text = "включи ночной режим света в спальне"};
  turn.Tick(mode, Generation("fc-request-1", "light_mode", "night"));
  REQUIRE(turn.result.tool_calls.size() == 1);
  REQUIRE(turn.action.error == "UNKNOWN_PARAMETER");
  REQUIRE(turn.events.size() == 1);
  turn.Tick({}, {});
  REQUIRE(turn.dialogue.pending.steps.size() == 1);
  REQUIRE(turn.dialogue.pending.steps[0].result.content == R"({"status": "fail", "error": "UNKNOWN_PARAMETER"})");

  // The model repeats the call.
  turn.Tick({}, Generation("fc-request-1/tool/1", "light_mode", "night"));
  REQUIRE(turn.action.kind == McpResolvedActionKind::NONE);
  REQUIRE(turn.completions.empty());
  REQUIRE(turn.events.size() == 1);
  const auto& reply = turn.events[0];
  REQUIRE(reply.kind == AssistantSessionEvent::Kind::REPLY);
  if (!configured) {
    REQUIRE(turn.result.error == "Repeated tool call");
    REQUIRE(reply.source == AssistantSessionEvent::Source::FALLBACK_TEXT);
    REQUIRE(reply.text == DemoParams(kOfflineAssistant, "assistant_session")["fallback_text"].as<std::string>());
    REQUIRE(turn.dialogue.turns.empty());
    return;
  }
  REQUIRE(turn.result.status == AssistantStatus::OK);
  REQUIRE(reply.source == AssistantSessionEvent::Source::ASSISTANT);
  REQUIRE(reply.text == refusal);
  REQUIRE(reply.reason.empty());
  REQUIRE(turn.dialogue.turns.size() == 1);
  const auto& done = turn.dialogue.turns[0];
  REQUIRE(done.user == mode.text);
  REQUIRE(done.steps.size() == 1);
  REQUIRE(done.steps[0].has_result);
  REQUIRE(done.assistant == refusal);

  // The next request sees the failed call and the reply, and its call is run.
  const UserQuery follow{.request_id = "fc-request-2", .text = "тогда просто включи его"};
  turn.Tick(follow, Generation("fc-request-2", "on_off", true));
  const auto& messages = turn.prompt.messages;
  REQUIRE(messages.size() >= 5);
  const auto last = messages.size() - 1;
  REQUIRE(messages[last].role == MessageRole::USER);
  REQUIRE(messages[last].content == follow.text);
  REQUIRE(messages[last - 1].role == MessageRole::ASSISTANT);
  REQUIRE(messages[last - 1].content == refusal);
  REQUIRE(messages[last - 2].role == MessageRole::TOOL);
  REQUIRE(messages[last - 2].content == R"({"status": "fail", "error": "UNKNOWN_PARAMETER"})");
  REQUIRE(messages[last - 3].role == MessageRole::ASSISTANT);
  REQUIRE(messages[last - 3].tool_calls.size() == 1);
  REQUIRE(turn.action.status == AssistantStatus::OK);
  REQUIRE(turn.action.tool_name == "homeassistant__GetLiveContext");
  REQUIRE(turn.events.size() == 1);
  REQUIRE(turn.events[0].kind == AssistantSessionEvent::Kind::STARTED);
}
