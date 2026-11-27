#include "giga_edge/modules/common/nodes/counter/node.h"

#include "giga_edge/modules/common/nodes/counter/params.h"

#include <cmath>
#include <limits>

namespace giga_edge {

CounterNode::CounterNode(const NodeBaseConfig& base_config, const CounterNodeParams& params)
  : NodeBase(base_config)
  , counter_value_(params.init_value)
  , counter_node_params_(params) {}

void CounterNode::Reset() {
  counter_value_ = counter_node_params_.init_value;
}

void CounterNode::ProcessOnce() {
  const auto& add_value = GetAdd();
  const auto& drop_value = GetNeedReset();

  if (drop_value.has_value() && drop_value.value()) {
    Reset();
  }

  if (
    ((add_value > 0) && (counter_value_ > (std::numeric_limits<float>::max() - add_value)))
    || ((add_value < 0) && (counter_value_ < (std::numeric_limits<float>::lowest() - add_value)))
  ) {
    Reset();
  }
  counter_value_ += add_value;

  PushOutput(counter_value_);
}

} // namespace giga_edge
