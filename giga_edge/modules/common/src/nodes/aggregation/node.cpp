#include "giga_edge/modules/common/nodes/aggregation/node.h"

#include "giga_edge/lib/aggregation/aggregation.h"

namespace giga_edge {

AggregationNode::AggregationNode(const NodeBaseConfig& base_config, const AggregationNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void AggregationNode::ProcessOnce() {
  const auto& input = GetInput();

  if (input.empty()) {
    PushOutput({});
    return;
  }
  PushOutput({Aggregate(input, params_.aggregation)});
}

} // namespace giga_edge