#pragma once

#include "params.h"

#include "giga_edge/lib/logical/logical_expression.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class SignalEventIsFiredNode : public NodeBase {
public:
  SignalEventIsFiredNode(const NodeBaseConfig& base_config, const SignalEventIsFiredNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_DYNAMIC_INPUTS(signal_event_is_fired);
  REGISTER_OUTPUT(signal_event_is_fired, output, Output, bool, "output shows if event was fired");

private:
  void ProcessEventImpl(const std::any& event) override;

private:
  bool event_was_fired_ = false;
  std::string signal_name_;
  SignalEventIsFiredResetStrategy reset_strategy_;
};

REGISTER_NODE(
  signal_event_is_fired, SignalEventIsFiredNode, SignalEventIsFiredNodeParams, "Node shows if certain event was fired"
);

} // namespace giga_edge
