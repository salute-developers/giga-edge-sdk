#include "giga_edge/modules/spotter/nodes/hotword_sleeping_event_processor/node.h"

namespace giga_edge {

HotwordSleepingEventProcessorNode::HotwordSleepingEventProcessorNode(
  const NodeBaseConfig& base_config, const HotwordSleepingEventProcessorParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

int32_t HotwordSleepingEventProcessorNode::LookupTimer(const std::string& hotword) const {
  const auto it = params_.timers.find(hotword);
  if (it != params_.timers.end()) {
    return it->second;
  }
  return params_.default_timer;
}

void HotwordSleepingEventProcessorNode::ProcessOnce() {
  for (auto it = sleeping_timer_current_value_.begin(); it != sleeping_timer_current_value_.end();) {
    if (it->second <= 1) {
      it = sleeping_timer_current_value_.erase(it);
    } else {
      --it->second;
      ++it;
    }
  }

  auto events = GetEvents();
  for (auto& event : events) {
    for (auto& single : event) {
      if (single.event_type != SpotterEventType::ACTIVATION && single.event_type != SpotterEventType::DISABLED) {
        continue;
      }

      if (sleeping_timer_current_value_.count(single.hot_word)) {
        single.event_type = single.event_type == SpotterEventType::ACTIVATION
          ? SpotterEventType::SUBACTIVATION
          : SpotterEventType::NONE;
        continue;
      }

      const int32_t reload = LookupTimer(single.hot_word);
      if (reload > 0) {
        sleeping_timer_current_value_.emplace(single.hot_word, reload);
      }
    }
  }
  PushEvents(events);
}

void HotwordSleepingEventProcessorNode::Reset() {
  sleeping_timer_current_value_.clear();
}

} // namespace giga_edge
