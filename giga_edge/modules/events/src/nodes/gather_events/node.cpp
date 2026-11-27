#include "giga_edge/modules/events/nodes/gather_events/node.h"

namespace giga_edge {

GatherEventsNode::GatherEventsNode(const NodeBaseConfig& base_config, const GatherEventsNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  for (int i = 1; i <= base_config.inputs.size(); ++i) {
    input_names_.insert("events_" + std::to_string(i));
  }
}

void GatherEventsNode::ProcessOnce() {
  SpotterEvent result;
  for (const auto& input_name : input_names_) {
    const auto& events = inputs_data_->GetSingle<std::vector<SpotterEvent>>(input_name);
    for (const auto& event : events) {
      for (const auto& single_event : event) {
        if (params_.event_types.find(single_event.event_type) != params_.event_types.end()) {
          result.push_back(single_event);
        }
      }
    }
  }

  PushEvent(result);
}

std::unordered_set<std::string> GatherEventsNode::GetDynamicInputs() {
  return {input_names_.begin(), input_names_.end()};
}

} // namespace giga_edge
