#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class TimerNode : public NodeBase {
public:
  TimerNode(const NodeBaseConfig& base_config, const TimerNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(timer, start, Start, bool, "Start the timer");
  REGISTER_OPTIONAL_INPUT(timer, reset, Reset, bool, "Whether to reset the timer. Called before starting timer");

  REGISTER_OUTPUT(timer, alarm, Alarm, bool, "Timer triggered");

private:
  TimerNodeParams params_;
  bool is_started_;
  int32_t counter_;
};

REGISTER_NODE(timer, TimerNode, TimerNodeParams, "Signals if timer is expired");

} // namespace giga_edge
