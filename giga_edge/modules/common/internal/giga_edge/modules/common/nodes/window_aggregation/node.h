#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/sliding_window/sliding_window.h"

namespace giga_edge {

class WindowAggregationNode : public NodeBase {
public:
  WindowAggregationNode(const NodeBaseConfig& base_config, const WindowAggregationNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(window_aggregation, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(window_aggregation, output, Output, std::vector<float>, "Output");


private:
  WindowAggregationNodeParams params_;

  std::vector<SlidingWindow> windows_;
  int32_t batch_num_;
};

REGISTER_NODE(
  window_aggregation, WindowAggregationNode, WindowAggregationNodeParams, "Aggregates data using window function"
);

} // namespace giga_edge