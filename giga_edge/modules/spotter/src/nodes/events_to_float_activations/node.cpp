#include "giga_edge/modules/spotter/nodes/events_to_float_activations/node.h"

namespace giga_edge {

namespace {

std::vector<float> EventsToFloatAcitvations(const std::vector<SpotterEvent>& events) {
  std::vector<float> scores;
  if (!events.empty()) {
    scores.assign(events[0].size(), 0);
  }
  for (const auto& event : events) {
    for (int i = 0; i < event.size(); ++i) {
      if (event[i].event_type == SpotterEventType::ACTIVATION) {
        scores[i] = 1;
      }
    }
  }
  return scores;
}

} // namespace

EventsToFloatActivationsNode::EventsToFloatActivationsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void EventsToFloatActivationsNode::ProcessOnce() {
  const auto& events = GetEvents();

  PushFloats(EventsToFloatAcitvations(events));
}

} // namespace giga_edge
