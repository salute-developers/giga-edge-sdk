#include "giga_edge/modules/spotter/sleeping_event_processor.h"

namespace giga_edge {

SleepingSpotterEventProcessor::SleepingSpotterEventProcessor(const SleepTimerParams& params)
  : params_(params)
  , sleeping_timer_current_value_(params_.sleeping_timer.size(), 0) {}

void SleepingSpotterEventProcessor::Process(std::vector<SpotterEvent>& events) {
  for (auto& event : events) {
    for (int i = 0; i < event.size(); ++i) {
      if (sleeping_timer_current_value_[i] > 0) {
        --sleeping_timer_current_value_[i];
      }

      if (event[i].event_type == SpotterEventType::ACTIVATION) {
        if (sleeping_timer_current_value_[i] > 0) {
          event[i].event_type = SpotterEventType::SUBACTIVATION;
        } else {
          sleeping_timer_current_value_[i] = params_.sleeping_timer[i];
        }
      } else if (event[i].event_type == SpotterEventType::DISABLED) {
        if (sleeping_timer_current_value_[i] > 0) {
          event[i].event_type = SpotterEventType::NONE;
        } else {
          sleeping_timer_current_value_[i] = params_.sleeping_timer[i];
        }
      }
    }
  }
}

void SleepingSpotterEventProcessor::Reset() {
  sleeping_timer_current_value_.assign(sleeping_timer_current_value_.size(), 0);
}

} // namespace giga_edge