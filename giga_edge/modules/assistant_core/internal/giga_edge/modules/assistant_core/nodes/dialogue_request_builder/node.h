#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/assistant_core/request_profile.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

struct DialogueRequestBuilderParams {
  std::string system_prompt;
  bool include_history = true;
  size_t max_history_turns = 0;
  bool include_scene = true;
  bool warm_on_scene_change = true;
  // Warm the system prompt as soon as new instructions arrive.
  bool warm_on_instructions = false;
  // The system prompt changed: warm it with the scene while no turn is running.
  bool warm_on_system_change = false;
  std::string user_prefix;
  std::string user_suffix;
  std::optional<float> temperature;
  int max_tokens = 0;
  RequestProfilePtr profile;
};

template<>
DialogueRequestBuilderParams ParseConfig(const YAML::Node& config);

class DialogueRequestBuilder : public NodeBase {
public:
  DialogueRequestBuilder(const NodeBaseConfig& base_config, const DialogueRequestBuilderParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  void AppendHistory(const DialogueHistory& history, AssistantRequest& request) const;
  void AppendTurn(const DialogueTurn& turn, AssistantRequest& request, const ToolResult* live_result) const;
  std::string UserContent(const std::string& text) const;

  REGISTER_INPUT(dialogue_request_builder, user_query, UserQuery, UserQuery, "Current user query or an empty tick.");
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder, dialogue_reset, DialogueReset, bool, "Suppresses generation on a dialogue reset tick."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    warm,
    Warm,
    bool,
    "A query is coming (a session started): when a scene is present, warm everything before it — system, scene "
    "and history."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    dialogue_history,
    DialogueHistory,
    DialogueHistory,
    "Dialogue window and the pending turn from dialogue_state_update (recursive)."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    scene_snapshot,
    SceneSnapshot,
    SceneSnapshot,
    "Rendered world state for the SCENE message."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder, tool_specs, ToolSpecs, ToolSpecs, "Tool catalog for native function calling."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    tool_result,
    ToolResult,
    ToolResult,
    "Result of the pending tool call; triggers the next hop."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    system_prompt,
    SystemPrompt,
    std::string,
    "Host-supplied system prompt; overrides the param when non-empty."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    instructions,
    Instructions,
    std::vector<std::string>,
    "Host instructions; the request profile renders them into the stable head of the user message."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    partial_text,
    PartialText,
    std::string,
    "Text recognized so far while the user still speaks; each change warms the KV with it as the user message."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    seed_history,
    SeedHistory,
    DialogueHistory,
    "Ground-truth history used when the dialogue history is empty."
  );
  REGISTER_OPTIONAL_INPUT(
    dialogue_request_builder,
    follow_up,
    FollowUp,
    bool,
    "The query answers the assistant's own question (session follow_up): the request is marked follow_up."
  );
  REGISTER_OUTPUT(
    dialogue_request_builder, assistant_request, AssistantRequest, AssistantRequest, "Request for any runner."
  );

  DialogueRequestBuilderParams params_;
  std::optional<std::vector<std::string>> instructions_;
  std::string warmed_partial_;
  size_t warm_count_ = 0;
  bool stale_history_ = false;
  std::string last_system_;
};

REGISTER_NODE(
  dialogue_request_builder,
  DialogueRequestBuilder,
  DialogueRequestBuilderParams,
  "Assembles an AssistantRequest from system prompt, scene, tools, dialogue window and the pending tool hop."
);

} // namespace giga_edge
