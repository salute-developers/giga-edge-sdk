#include "giga_edge/modules/synthesis_text/nodes/synthesis_text/node.h"

namespace giga_edge {

SynthesisTextNode::SynthesisTextNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config)
  , synthesis_text_({})
  , reset_synthesis_text_(false) {
  for (const auto& input : base_config.inputs) {
    inputs_.insert(input.first);
  }
}

void SynthesisTextNode::ProcessOnce() {
  PushSynthesisText(synthesis_text_);
  synthesis_text_.clear();
  PushResetSynthesisText(reset_synthesis_text_);
  reset_synthesis_text_ = false;
}

void SynthesisTextNode::Reset() {
  synthesis_text_.clear();
  reset_synthesis_text_ = false;
}

void SynthesisTextNode::ProcessEventImpl(const std::any& event) {
  if (event.type() == typeid(SetSynthesisTextEvent)) {
    ProcessSetSynthesisText(std::any_cast<const SetSynthesisTextEvent&>(event));
  } else if (event.type() == typeid(ResetSynthesisTextEvent)) {
    ProcessResetSynthesisText(std::any_cast<const ResetSynthesisTextEvent&>(event));
  }
}

void SynthesisTextNode::ProcessSetSynthesisText(const SetSynthesisTextEvent& event) {
  synthesis_text_ = event.synthesis_text;
}

void SynthesisTextNode::ProcessResetSynthesisText([[maybe_unused]] const ResetSynthesisTextEvent& event) {
  reset_synthesis_text_ = true;
}

std::unordered_set<std::string> SynthesisTextNode::GetDynamicInputs() {
  return inputs_;
}

} // namespace giga_edge
