#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/dialogue.h"
#include "giga_edge/modules/assistant_core/nodes/dialogue_request_builder/node.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <functional>
#include <string>
#include <utility>

namespace giga_edge {
namespace {

AssistantMessage Message(MessageRole role, std::string content) {
  AssistantMessage message;
  message.role = role;
  message.content = std::move(content);
  return message;
}

} // namespace

template<>
DialogueRequestBuilderParams ParseConfig(const YAML::Node& config) {
  DialogueRequestBuilderParams result;
  result.system_prompt = config["system_prompt"].as<std::string>(result.system_prompt);
  result.include_history = config["include_history"].as<bool>(result.include_history);
  result.max_history_turns = config["max_history_turns"].as<size_t>(result.max_history_turns);
  result.include_scene = config["include_scene"].as<bool>(result.include_scene);
  result.warm_on_scene_change = config["warm_on_scene_change"].as<bool>(result.warm_on_scene_change);
  result.warm_on_instructions = config["warm_on_instructions"].as<bool>(result.warm_on_instructions);
  result.warm_on_system_change = config["warm_on_system_change"].as<bool>(result.warm_on_system_change);
  result.user_prefix = config["user_prefix"].as<std::string>(result.user_prefix);
  result.user_suffix = config["user_suffix"].as<std::string>(result.user_suffix);
  if (config["temperature"]) {
    result.temperature = config["temperature"].as<float>();
  }
  result.max_tokens = config["max_tokens"].as<int>(result.max_tokens);
  if (const auto profile = config["profile"]; profile) {
    result.profile = profile.IsScalar()
      ? RequestProfileRegistry::Create(profile.as<std::string>(), YAML::Node())
      : RequestProfileRegistry::Create(profile["name"].as<std::string>(), profile);
  }
  return result;
}

DialogueRequestBuilder::DialogueRequestBuilder(
  const NodeBaseConfig& base_config, const DialogueRequestBuilderParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void DialogueRequestBuilder::Reset() {
  instructions_.reset();
  warmed_partial_.clear();
  stale_history_ = true;
}

std::string DialogueRequestBuilder::UserContent(const std::string& text) const {
  if (params_.profile != nullptr) {
    return params_.profile->UserMessage(instructions_.value_or(std::vector<std::string>{}), text);
  }
  return params_.user_prefix + text + params_.user_suffix;
}

void DialogueRequestBuilder::AppendTurn(
  const DialogueTurn& turn, AssistantRequest& request, const ToolResult* live_result
) const {
  request.messages.push_back(Message(MessageRole::USER, UserContent(turn.user)));
  for (size_t i = 0; i < turn.steps.size(); ++i) {
    const auto& step = turn.steps[i];
    AssistantMessage call = Message(MessageRole::ASSISTANT, step.assistant_raw);
    call.tool_calls.push_back(step.call);
    request.messages.push_back(std::move(call));

    const bool last = i + 1 == turn.steps.size();
    if (step.has_result) {
      AssistantMessage result = Message(MessageRole::TOOL, step.result.content);
      result.tool_call_id = step.result.call_id;
      request.messages.push_back(std::move(result));
    } else if (last && live_result != nullptr) {
      AssistantMessage result = Message(MessageRole::TOOL, live_result->content);
      result.tool_call_id = live_result->call_id;
      request.messages.push_back(std::move(result));
    }
  }
  if (turn.completed && !turn.assistant.empty()) {
    request.messages.push_back(Message(MessageRole::ASSISTANT, turn.assistant));
  }
}

void DialogueRequestBuilder::AppendHistory(const DialogueHistory& history, AssistantRequest& request) const {
  if (!params_.include_history) {
    return;
  }
  const auto& turns = history.turns;
  size_t start = 0;
  if (params_.max_history_turns > 0 && turns.size() > params_.max_history_turns) {
    start = turns.size() - params_.max_history_turns;
  }
  for (size_t i = start; i < turns.size(); ++i) {
    AppendTurn(turns[i], request, nullptr);
  }
}

void DialogueRequestBuilder::ProcessOnce() {
  const bool stale = std::exchange(stale_history_, false);
  const auto& query = GetUserQuery();
  const auto history = GetDialogueHistory();
  const auto scene = GetSceneSnapshot();
  const auto tools = GetToolSpecs();
  const auto result = GetToolResult();
  const auto host_system = GetSystemPrompt();
  const auto seed = GetSeedHistory();
  const auto partial = GetPartialText();

  bool instructions_changed = false;
  if (params_.profile != nullptr) {
    if (const auto items = GetInstructions(); items.has_value()) {
      instructions_changed = instructions_ != items->get();
      instructions_ = items->get();
    }
  }

  AssistantRequest request;
  if (const auto reset = GetDialogueReset(); reset.has_value() && reset->get()) {
    PushAssistantRequest(std::move(request));
    return;
  }
  request.generation.temperature = params_.temperature;
  request.generation.max_tokens = params_.max_tokens;

  if (query.status == AssistantStatus::ERROR) {
    request.request_id = query.request_id;
    request.status = AssistantStatus::ERROR;
    request.error = query.error.empty() ? "user query error" : query.error;
    PushAssistantRequest(std::move(request));
    return;
  }
  const bool scene_failed = params_.include_scene && scene.has_value() && scene->get().status == AssistantStatus::ERROR;
  if (!query.text.empty() && scene_failed) {
    request.request_id = query.request_id;
    request.status = AssistantStatus::ERROR;
    request.error = scene->get().error.empty() ? "scene snapshot error" : scene->get().error;
    PushAssistantRequest(std::move(request));
    return;
  }

  std::string system = host_system.has_value() && !host_system->get().empty()
    ? host_system->get()
    : params_.system_prompt;
  if (system.empty() && params_.profile != nullptr) {
    system = params_.profile->SystemPrompt(instructions_.value_or(std::vector<std::string>{}));
  }
  const bool scene_wired = params_.include_scene && scene.has_value();
  const std::string scene_text = scene_wired ? TrimTrailingNewlines(scene->get().text) : "";
  const std::string scene_version = scene_wired ? scene->get().version : "";
  const bool scene_usable = scene_wired && !scene_failed && (!scene_text.empty() || scene->get().allow_empty);

  const DialogueHistory* window = nullptr;
  if (history.has_value() && !stale) {
    window = &history->get();
  }
  if ((window == nullptr || (window->turns.empty() && !window->has_pending)) && seed.has_value()) {
    window = &seed->get();
  }

  const bool new_query = !query.text.empty();
  // After a batch call the next one is already generated: generation runs after the result of the last one.
  const bool hop = !new_query && result.has_value() && window != nullptr
    && MatchesPendingToolResult(*window, result->get()) && window->pending.steps.back().queued_calls == 0;

  if (!hop && !new_query) {
    const bool scene_idle = scene_usable && !scene_text.empty() && window != nullptr && !window->has_pending;
    const auto warm_input = GetWarm();
    const bool warm_next = scene_idle && warm_input.has_value() && warm_input->get();
    const bool scene_warm = scene_idle && params_.warm_on_scene_change && scene_version != window->last_scene_version;
    const bool system_warm = scene_idle && params_.warm_on_system_change && system != last_system_;
    const bool idle_window = window == nullptr || !window->has_pending;
    const bool instructions_warm = params_.warm_on_instructions && instructions_changed && idle_window;
    const bool partial_warm = partial.has_value() && !partial->get().empty() && partial->get() != warmed_partial_
      && idle_window;
    if (!warm_next && !scene_warm && !system_warm && !instructions_warm && !partial_warm) {
      PushAssistantRequest(std::move(request));
      return;
    }
    request.kind = AssistantRequestKind::WARM;
    request.request_id = instructions_warm || partial_warm
      ? "prewarm:" + std::to_string(++warm_count_)
      : "warm:" + scene_version;
    if (params_.warm_on_system_change && !system.empty()) {
      // The runtime drops a repeated id: a warm-up with a different system prompt gets a different id.
      request.request_id += "#" + std::to_string(std::hash<std::string>{}(system));
    }
    last_system_ = system;
    request.messages.push_back(Message(MessageRole::SYSTEM, system));
    if (scene_usable && !scene_text.empty()) {
      request.messages.push_back(Message(MessageRole::SCENE, scene_text));
    }
    if (partial_warm) {
      // The beginning of the future request: GENERATE on the full text reuses this prefix from the KV cache.
      warmed_partial_ = partial->get();
      if (window != nullptr) {
        AppendHistory(*window, request);
      }
      request.messages.push_back(Message(MessageRole::USER, UserContent(warmed_partial_)));
    } else if (warm_next && !window->turns.empty()) {
      request.request_id += ":" + std::to_string(window->turns.size()) + ":" + window->turns.back().request_id;
      AppendHistory(*window, request);
    }
    if (tools.has_value()) {
      request.tools = tools->get().tools;
    }
    PushAssistantRequest(std::move(request));
    return;
  }

  request.kind = AssistantRequestKind::GENERATE;
  last_system_ = system;
  request.messages.push_back(Message(MessageRole::SYSTEM, system));
  if (tools.has_value()) {
    request.tools = tools->get().tools;
  }

  if (hop) {
    const auto& pending = window->pending;
    request.request_id = result->get().call_id;
    if (scene_wired) {
      request.messages.push_back(Message(MessageRole::SCENE, pending.scene_text));
    }
    AppendHistory(*window, request);
    AppendTurn(pending, request, &result->get());
    PushAssistantRequest(std::move(request));
    return;
  }

  warmed_partial_.clear();
  request.request_id = query.request_id;
  const auto follow_up = GetFollowUp();
  request.follow_up = follow_up.has_value() && follow_up->get();
  if (scene_usable) {
    request.messages.push_back(Message(MessageRole::SCENE, scene_text));
  }
  if (window != nullptr) {
    AppendHistory(*window, request);
  }
  request.messages.push_back(Message(MessageRole::USER, UserContent(query.text)));
  PushAssistantRequest(std::move(request));
}

} // namespace giga_edge
