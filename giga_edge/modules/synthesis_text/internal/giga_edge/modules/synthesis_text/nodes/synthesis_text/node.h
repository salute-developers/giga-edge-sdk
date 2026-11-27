#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/synthesis_text/events.h"

namespace giga_edge {

class SynthesisTextNode : public NodeBase {
public:
  explicit SynthesisTextNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_DYNAMIC_INPUTS(synthesis_text);
  REGISTER_OUTPUT(synthesis_text, synthesis_text, SynthesisText, std::string, "Synthesis text has started");
  REGISTER_OUTPUT(synthesis_text, reset_synthesis_text, ResetSynthesisText, bool, "Reset sytnehsis text has occured");

private:
  void ProcessEventImpl(const std::any& event) override;

  void ProcessSetSynthesisText(const SetSynthesisTextEvent& event);
  void ProcessResetSynthesisText(const ResetSynthesisTextEvent& event);

private:
  std::string synthesis_text_;
  bool reset_synthesis_text_;
  std::unordered_set<std::string> inputs_;
};

REGISTER_NODE(synthesis_text, SynthesisTextNode, "A node for firing synthesis_text events as outputs.");

} // namespace giga_edge
