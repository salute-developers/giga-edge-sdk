#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/dialogue.h"
#include "giga_edge/modules/assistant_core/nodes/dialogue_state_update/node.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace giga_edge {
namespace {

// For a source without provenance (cloud, a pass-through node) the history is the raw reply.
const std::string& HistoryText(const AssistantResult& result) {
  return result.history_text.empty() ? result.raw_text : result.history_text;
}

} // namespace

template<>
DialogueStateUpdateParams ParseConfig(const YAML::Node& config) {
  DialogueStateUpdateParams result;
  result.max_turns = config["max_turns"].as<size_t>(result.max_turns);
  // The turn that has just finished always stays.
  result.trim_to_turns = std::min(
    std::max<size_t>(config["trim_to_turns"].as<size_t>(result.max_turns), 1), result.max_turns
  );
  result.drop_truncated_reply = config["drop_truncated_reply"].as<bool>(result.drop_truncated_reply);
  return result;
}

DialogueStateUpdate::DialogueStateUpdate(const NodeBaseConfig& base_config, const DialogueStateUpdateParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void DialogueStateUpdate::DoInitRecursiveOutputs() {
  PushDialogueHistory(DialogueHistory{});
}

void DialogueStateUpdate::ProcessOnce() {
  DialogueHistory history = GetPreviousHistory();
  if (std::exchange(cleared_, false)) {
    history = DialogueHistory{};
  }
  const auto& query = GetUserQuery();
  const auto& result = GetAssistantResult();
  const auto tool_result = GetToolResult();
  const auto scene = GetSceneSnapshot();

  const bool tracing = HasTurnTraceOutput();
  const auto now = tracing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  TurnTrace trace;
  bool trace_changed = false;

  if (const auto reset = GetDialogueReset(); reset.has_value() && reset->get()) {
    PushDialogueHistory(DialogueHistory{});
    PushTurnTrace({});
    return;
  }

  if (const auto cancel = GetCancel(); cancel.has_value() && cancel->get()) {
    // The result of the cancelled round arrives in the same tick: it goes to the trace before the turn is cleared.
    if (
      tracing && history.has_pending && result.is_final && !result.request_id.empty()
      && result.request_id == PendingGenerationId(history.pending)
    ) {
      trace_.Round(result, now);
      trace = trace_.Finish(result, now);
    }
    history.pending = DialogueTurn{};
    history.has_pending = false;
    history.queued = DialogueTurn{};
    history.has_queued = false;
  }

  if (scene.has_value()) {
    history.last_scene_version = scene->get().version;
  }

  if (!query.text.empty()) {
    history.queued = DialogueTurn{};
    history.queued.request_id = query.request_id;
    history.queued.user = query.text;
    if (scene.has_value()) {
      history.queued.scene_text = TrimTrailingNewlines(scene->get().text);
      history.queued.scene_version = scene->get().version;
    }
    history.has_queued = true;
    trace_.Queue(now);
  }

  // The turn becomes current only when the LLM has taken it: with on_new_request=ignore the request may be dropped.
  if (history.has_queued && !result.request_id.empty() && result.request_id == history.queued.request_id) {
    history.pending = std::move(history.queued);
    history.has_pending = true;
    history.queued = DialogueTurn{};
    history.has_queued = false;
    if (tracing) {
      trace_.Start(history.pending.request_id, history.pending.user, now);
      trace_changed = true;
    }
  }

  if (tool_result.has_value() && MatchesPendingToolResult(history, tool_result->get())) {
    auto& steps = history.pending.steps;
    if (!steps.empty() && !steps.back().has_result) {
      steps.back().result = tool_result->get();
      steps.back().has_result = true;
      if (tracing && trace_.ToolResult(tool_result->get().content, now)) {
        trace_changed = true;
      }
    }
  }

  if (
    history.has_pending && result.is_final && !result.request_id.empty()
    && result.request_id == PendingGenerationId(history.pending)
  ) {
    const bool traced = tracing && trace_.IsActive();
    if (traced) {
      trace_.Round(result, now);
      trace_changed = true;
    }
    if (
      result.status != AssistantStatus::OK || result.finish_reason == StreamFinishReason::CANCELLED
      || result.finish_reason == StreamFinishReason::DECLINED
      || (params_.drop_truncated_reply && result.finish_reason == StreamFinishReason::MAX_TOKENS)
    ) {
      if (traced) {
        trace = trace_.Finish(result, now);
      }
      history.has_pending = false;
      history.pending = DialogueTurn{};
    } else if (!result.tool_calls.empty()) {
      ToolStep step;
      step.assistant_raw = HistoryText(result);
      step.call = result.tool_calls.front();
      step.queued_calls = result.queued_calls;
      history.pending.steps.push_back(std::move(step));
    } else {
      if (traced) {
        trace = trace_.Finish(result, now);
      }
      history.pending.assistant = HistoryText(result);
      history.pending.completed = true;
      if (params_.max_turns > 0) {
        history.pending.scene_text.clear();
        history.pending.scene_version.clear();
        history.turns.push_back(std::move(history.pending));
        if (history.turns.size() > params_.max_turns) {
          history.turns.erase(
            history.turns.begin(),
            history.turns.begin() + static_cast<std::ptrdiff_t>(history.turns.size() - params_.trim_to_turns)
          );
        }
      }
      history.has_pending = false;
      history.pending = DialogueTurn{};
    }
  }

  // Snapshot: on a timeout or a cancel there will be no final result.
  if (trace.request_id.empty() && trace_changed) {
    trace = trace_.Snapshot(now);
  }
  PushDialogueHistory(std::move(history));
  PushTurnTrace(std::move(trace));
}

} // namespace giga_edge
