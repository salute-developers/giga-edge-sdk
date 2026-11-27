#include "giga_edge/modules/events/nodes/concat_events/node.h"

namespace giga_edge {

ConcatEventsNode::ConcatEventsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  for (int i = 1; i <= base_config.inputs.size(); ++i) {
    input_names_.insert("events_" + std::to_string(i));
  }
}

void ConcatEventsNode::ProcessOnce() {
  std::vector<SpotterEvent> result;
  for (const auto& input_name : input_names_) {
    const auto& events = inputs_data_->GetSingle<std::vector<SpotterEvent>>(input_name);
    result.insert(result.end(), events.begin(), events.end());
  }

  PushEvents(result);
}

std::unordered_set<std::string> ConcatEventsNode::GetDynamicInputs() {
  return {input_names_.begin(), input_names_.end()};
}

} // namespace giga_edge
