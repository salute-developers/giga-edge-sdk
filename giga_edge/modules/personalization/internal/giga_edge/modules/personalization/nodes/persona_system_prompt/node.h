#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/personalization/persona.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

namespace giga_edge {

class PersonaSystemPromptNode : public NodeBase {
public:
  PersonaSystemPromptNode(const NodeBaseConfig& base_config, const persona::SystemPromptParams& params)
    : NodeBase(base_config)
    , params_(params) {}

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(persona_system_prompt, gender, Gender, std::string, "Character gender, as persona gives it.");
  REGISTER_INPUT(persona_system_prompt, pronoun, Pronoun, std::string, "Pronoun, as persona gives it.");
  REGISTER_OPTIONAL_INPUT(
    persona_system_prompt, room, Room, std::string, "Room of the speaker; empty — unknown, no room line."
  );
  REGISTER_OUTPUT(
    persona_system_prompt,
    system_prompt,
    SystemPrompt,
    std::string,
    "System prompt of the model for the gender and pronoun; the default one without a gender. With room_template "
    "and a room, the room line follows."
  );

  persona::SystemPromptParams params_;
  std::optional<std::pair<std::string, std::string>> key_;
  std::string room_;
  std::string prompt_;
  std::set<std::string> warned_;
};

REGISTER_NODE(
  persona_system_prompt,
  PersonaSystemPromptNode,
  persona::SystemPromptParams,
  "Picks the model's system prompt for the character gender and pronoun."
);

} // namespace giga_edge
