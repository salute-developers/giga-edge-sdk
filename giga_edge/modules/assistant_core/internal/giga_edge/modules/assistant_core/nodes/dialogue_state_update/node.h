#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/assistant_core/turn_trace.h"

#include <cstddef>

namespace giga_edge {

struct DialogueStateUpdateParams {
  size_t max_turns = 0;
  size_t trim_to_turns = 0;
  bool drop_truncated_reply = false;
};

template<>
DialogueStateUpdateParams ParseConfig(const YAML::Node& config);

class DialogueStateUpdate : public NodeBase {
public:
  DialogueStateUpdate(const NodeBaseConfig& base_config, const DialogueStateUpdateParams& params);

  void ProcessOnce() override;
  void Reset() override {
    cleared_ = true;
  }

private:
  void DoInitRecursiveOutputs() override;

  REGISTER_INPUT(
    dialogue_state_update,
    previous_history,
    PreviousHistory,
    DialogueHistory,
    "History from the previous tick (recursive)."
  );
  REGISTER_INPUT(dialogue_state_update, user_query, UserQuery, UserQuery, "Current user query or an empty tick.");
  REGISTER_INPUT(
    dialogue_state_update, assistant_result, AssistantResult, AssistantResult, "Accumulated answer of the current hop."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_state_update, tool_result, ToolResult, ToolResult, "Tool result attached to the pending step."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_state_update, scene_snapshot, SceneSnapshot, SceneSnapshot, "Scene captured when a turn starts."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_state_update, dialogue_reset, DialogueReset, bool, "Clears the window and the pending turn."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_state_update, cancel, Cancel, bool, "Drops the unfinished turn, finished ones stay."
  );
  REGISTER_RECURSIVE_OUTPUT(
    dialogue_state_update, dialogue_history, DialogueHistory, DialogueHistory, "Updated dialogue history."
  );
  REGISTER_OPTIONAL_OUTPUT(
    dialogue_state_update,
    turn_trace,
    TurnTrace,
    TurnTrace,
    "The current turn on each tick it changes: LLM rounds and tool calls with timings; the last one, finished, comes "
    "with the final result; empty otherwise."
  );

  DialogueStateUpdateParams params_;
  bool cleared_ = false;
  TurnTraceRecorder trace_;
};

REGISTER_NODE(
  dialogue_state_update,
  DialogueStateUpdate,
  DialogueStateUpdateParams,
  "Keeps the dialogue window and the pending turn with its tool steps across ticks. drop_truncated_reply (default "
  "false) drops a turn whose reply is cut by the token limit instead of keeping it."
);

} // namespace giga_edge
