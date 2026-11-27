#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class AggregationNode : public NodeBase {
public:
  AggregationNode(const NodeBaseConfig& base_config, const AggregationNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(aggregation, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(aggregation, output, Output, std::vector<float>, "Output");


private:
  AggregationNodeParams params_;
};

REGISTER_NODE(aggregation, AggregationNode, AggregationNodeParams, "Calculates aggregtaion function on input");

} // namespace giga_edge
