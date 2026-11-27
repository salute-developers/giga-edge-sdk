#include "giga_edge/modules/events/nodes/filter_events/node.h"

namespace giga_edge {

FilterEventsNode::FilterEventsNode(const NodeBaseConfig& base_config, const FilterEventsNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void FilterEventsNode::ProcessOnce() {
  const auto& events = GetEvents();
  SpotterEvent result;
  for (const auto& event : events) {
    for (const auto& single_event : event) {
      if (params_.event_types.find(single_event.event_type) != params_.event_types.end()) {
        result.push_back(single_event);
      }
    }
  }

  PushEvent(std::move(result));
}

} // namespace giga_edge
