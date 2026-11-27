#include "giga_edge/modules/spotter/nodes/event_generator/node.h"

namespace giga_edge {

EventGeneratorNode::EventGeneratorNode(const NodeBaseConfig& base_config, const EventGeneratorParams& params)
  : NodeBase(base_config)
  , event_generator_(params)
  , num_frames_after_reset_synthesis_text_(0)
  , need_force_reset_synthesis_text_(false) {}

void EventGeneratorNode::ProcessOnce() {
  if (event_generator_.GetParams().reset_synthesis_text_delay > 0 && num_frames_after_reset_synthesis_text_ > 0) {
    if (num_frames_after_reset_synthesis_text_++ > event_generator_.GetParams().reset_synthesis_text_delay) {
      event_generator_.ResetSynthesisText(need_force_reset_synthesis_text_);
      num_frames_after_reset_synthesis_text_ = 0;
    }
  }

  const auto& predictions = GetPredictions();

  if (!predictions.empty()) {
    PushEvents(event_generator_.GenerateSpotterEvents(predictions));
  } else {
    PushEvents({});
  }
}

void EventGeneratorNode::Reset() {
  num_frames_after_reset_synthesis_text_ = 0;
  need_force_reset_synthesis_text_ = false;
}

void EventGeneratorNode::ProcessEventImpl(const std::any& event) {
  if (event.type() == typeid(SetSynthesisTextEvent)) {
    ProcessSetSynthesisText(std::any_cast<const SetSynthesisTextEvent&>(event));
  } else if (event.type() == typeid(ResetSynthesisTextEvent)) {
    ProcessResetSynthesisText(std::any_cast<const ResetSynthesisTextEvent&>(event));
  } else if (event.type() == typeid(SetActiveNameEvent)) {
    ProcessSetActiveName(std::any_cast<const SetActiveNameEvent&>(event));
  } else if (event.type() == typeid(SetActivationModeEvent)) {
    ProcessSetActivationMode(std::any_cast<const SetActivationModeEvent&>(event));
  }
}

void EventGeneratorNode::ProcessSetActiveName(const SetActiveNameEvent& event) {
  event_generator_.SetActiveName(event.active_name);
}

void EventGeneratorNode::ProcessSetSynthesisText(const SetSynthesisTextEvent& event) {
  event_generator_.SetSynthesisText(event.synthesis_text, event.force);
}

void EventGeneratorNode::ProcessResetSynthesisText(const ResetSynthesisTextEvent& event) {
  if (event_generator_.GetParams().reset_synthesis_text_delay == 0) {
    event_generator_.ResetSynthesisText(event.force);
    num_frames_after_reset_synthesis_text_ = 0;
  } else {
    num_frames_after_reset_synthesis_text_ = 1;
    need_force_reset_synthesis_text_ = event.force;
  }
}

void EventGeneratorNode::ProcessSetActivationMode(const SetActivationModeEvent& event) {
  if (event.model_name == "all" || event.model_name == event_generator_.GetParams().model_name) {
    event_generator_.SetActivationMode(event.activaion_mode);
  }
}

} // namespace giga_edge