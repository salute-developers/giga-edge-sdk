#include "giga_edge/modules/events/nodes/filter_hotword_events/node.h"

namespace giga_edge {

FilterHotwordEventsNode::FilterHotwordEventsNode(
  const NodeBaseConfig& base_config, const FilterHotwordEventsNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void FilterHotwordEventsNode::ProcessOnce() {
  const auto& events = GetEvents();
  std::vector<SpotterEvent> filtered_events;
  for (const auto& event : events) {
    SpotterEvent filtered_event;
    for (const auto& single_event : event) {
      if (params_.hotwords.find(single_event.hot_word) != params_.hotwords.end()) {
        filtered_event.push_back(single_event);
      }
    }
    if (!filtered_event.empty()) {
      filtered_events.emplace_back(std::move(filtered_event));
    }
  }

  PushFilteredEvents(std::move(filtered_events));
}

} // namespace giga_edge
