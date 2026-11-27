#include "giga_edge/modules/spotter/nodes/has_activation/node.h"

namespace giga_edge {

HasActivationNode::HasActivationNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void HasActivationNode::ProcessOnce() {
  const auto& spotter_event = GetEvents();

  bool result = false;
  for (const auto& events : spotter_event) {
    for (const auto& event : events) {
      if (event.event_type == SpotterEventType::ACTIVATION) {
        result = true;
        break;
      }
    }
  }

  PushOutput(result);
}

} // namespace giga_edge
