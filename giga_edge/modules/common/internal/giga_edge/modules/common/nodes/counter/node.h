#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class CounterNode : public NodeBase {
public:
  CounterNode(const NodeBaseConfig& base_config, const CounterNodeParams& counter_node_params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(counter, add, Add, float, "The value to be added to the currently saved counter");
  REGISTER_OPTIONAL_INPUT(counter, need_reset, NeedReset, bool, "Whether to reset counter (optional)");

  REGISTER_OUTPUT(counter, output, Output, float, "Saved counter value");

private:
  float counter_value_;
  CounterNodeParams counter_node_params_;
};

REGISTER_NODE(
  counter,
  CounterNode,
  CounterNodeParams,
  "Node used as a counter: stores the value, can be reset to the original\
                                                      and can be changed by adding and subtracting values. NeedReset takes higher priority.\
                                                      If the counter overflows, it resets to the initial value."
);

} // namespace giga_edge
