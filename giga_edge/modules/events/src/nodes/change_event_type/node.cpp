#include "giga_edge/modules/events/nodes/change_event_type/node.h"

namespace giga_edge {

ChangeEventTypeNode::ChangeEventTypeNode(const NodeBaseConfig& base_config, const ChangeEventTypeNodeParams& params)
  : NodeBase(base_config) {
  input_event_type_ = params.input_event_type;
  output_event_type_ = params.output_event_type;
}

void ChangeEventTypeNode::ProcessOnce() {
  bool flag = true;

  if (const auto& input = GetFlag(); input.has_value()) {
    flag = input.value().get();
  }

  if (flag) {
    std::vector<SpotterEvent> events = GetInputEvents();
    for (auto& event : events) {
      for (auto& single_event : event) {
        if (single_event.event_type == input_event_type_) {
          single_event.event_type = output_event_type_;
        }
      }
    }
    PushEvents(std::move(events));
  } else {
    PushEvents(GetInputEvents());
  }
}

} // namespace giga_edge
