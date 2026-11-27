#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"

namespace giga_edge {

inline bool MatchesPendingToolResult(const DialogueHistory& history, const ToolResult& result) {
  if (!history.has_pending || history.pending.steps.empty() || !IsReadyToolResult(result)) {
    return false;
  }
  const auto& step = history.pending.steps.back();
  return !step.has_result && !step.call.id.empty() && step.call.id == result.call_id;
}

inline std::string PendingGenerationId(const DialogueTurn& turn) {
  if (turn.steps.empty()) {
    return turn.request_id;
  }
  const auto& step = turn.steps.back();
  return step.has_result ? step.result.call_id : std::string();
}

} // namespace giga_edge
