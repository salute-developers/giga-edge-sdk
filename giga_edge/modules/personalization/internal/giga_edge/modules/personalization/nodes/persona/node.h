#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/personalization/environment.h"
#include "giga_edge/modules/personalization/persona.h"

#include <cstdint>
#include <optional>
#include <string>

namespace giga_edge {

class PersonaNode : public NodeBase {
public:
  PersonaNode(const NodeBaseConfig& base_config, const persona::PersonaParams& params);

  void ProcessOnce() override;
  void Reset() override {}

private:
  void Refresh(const Personalization& personalization);

  REGISTER_INPUT(persona, tick, Tick, bool, "Any per-tick flag: the node follows the graph's ticks.");
  REGISTER_OUTPUT(
    persona, tts_voice, TtsVoice, std::string, "Synthesis voice of the host's voice; empty — the synthesis default."
  );
  REGISTER_OUTPUT(
    persona, gender, Gender, std::string, "Character gender of the host's voice: male or female; empty — no voice."
  );
  REGISTER_OUTPUT(persona, pronoun, Pronoun, std::string, "How to address the user: formal or informal.");

  persona::PersonaParams params_;
  PersonalizationEnvironmentPtr environment_;
  std::optional<uint64_t> revision_;
  std::string tts_voice_;
  std::string gender_;
  std::string pronoun_;
};

REGISTER_NODE(
  persona,
  PersonaNode,
  persona::PersonaParams,
  "Host personalization (voice and pronoun) as the model's synthesis voice, character gender and pronoun."
);

} // namespace giga_edge
