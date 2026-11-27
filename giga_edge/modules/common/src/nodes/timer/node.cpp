#include "giga_edge/modules/common/nodes/timer/node.h"

namespace giga_edge {

TimerNode::TimerNode(const NodeBaseConfig& base_config, const TimerNodeParams& params)
  : NodeBase(base_config)
  , params_(params)
  , is_started_(false)
  , counter_(0) {}

void TimerNode::ProcessOnce() {
  const auto start = GetStart();
  const auto reset = GetReset().has_value() && GetReset().value();

  if (reset) {
    Reset();
  }

  if (is_started_) {
    --counter_;
  }

  if (start) {
    if (is_started_) {
      switch (params_.restart_strategy) {
        case TimerNodeParams::RestartStrategy::RESTART:
          counter_ = params_.timer;
          break;
        case TimerNodeParams::RestartStrategy::IGNORE:
          break;
      }
    } else {
      is_started_ = true;
      counter_ = params_.timer;
    }
  }

  if (!is_started_) {
    PushAlarm(false);
    return;
  }

  if (!counter_) {
    PushAlarm(true);
    is_started_ = false;
    return;
  }

  if (params_.continuous_alarm) {
    PushAlarm(true);
  } else {
    PushAlarm(false);
  }
}

void TimerNode::Reset() {
  is_started_ = false;
}

} // namespace giga_edge
