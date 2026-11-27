#include "giga_edge/modules/synthesis_text/nodes/synthesis_text_buffer/node.h"

namespace giga_edge {

SynthesisTextBufferNode::SynthesisTextBufferNode(
  const NodeBaseConfig& base_config, const SynthesisTextBufferParams& synthesis_text_buffer_params
)
  : NodeBase(base_config)
  , synthesis_text_buffer_params_(synthesis_text_buffer_params)
  , num_frames_after_reset_synthesis_text_(0) {
  synthesis_text_ = "";
}

void SynthesisTextBufferNode::ProcessOnce() {
  // A delayed reset: the text is cleared once reset_synthesis_text_delay ticks have passed since the reset event.
  if (synthesis_text_buffer_params_.reset_synthesis_text_delay > 0 && num_frames_after_reset_synthesis_text_ > 0) {
    if (num_frames_after_reset_synthesis_text_++ > synthesis_text_buffer_params_.reset_synthesis_text_delay) {
      Reset();
    }
  }

  const auto flag = GetAllowOutput();
  if (!flag) {
    PushSynthesisText("");
    return;
  }

  PushSynthesisText(synthesis_text_);
}

void SynthesisTextBufferNode::Reset() {
  synthesis_text_ = "";
  num_frames_after_reset_synthesis_text_ = 0;
}

void SynthesisTextBufferNode::ProcessEventImpl(const std::any& event) {
  if (event.type() == typeid(SetSynthesisTextEvent)) {
    ProcessSetSynthesisText(std::any_cast<const SetSynthesisTextEvent&>(event));
  } else if (event.type() == typeid(ResetSynthesisTextEvent)) {
    ProcessResetSynthesisText(std::any_cast<const ResetSynthesisTextEvent&>(event));
  }
}

void SynthesisTextBufferNode::ProcessSetSynthesisText(const SetSynthesisTextEvent& event) {
  synthesis_text_ = event.synthesis_text;
}

void SynthesisTextBufferNode::ProcessResetSynthesisText([[maybe_unused]] const ResetSynthesisTextEvent& event) {
  if (synthesis_text_buffer_params_.reset_synthesis_text_delay == 0) {
    Reset();
  } else {
    num_frames_after_reset_synthesis_text_ = 1;
  }
}

} // namespace giga_edge
