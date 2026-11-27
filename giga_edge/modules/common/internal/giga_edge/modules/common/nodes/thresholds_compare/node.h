#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ThresholdsCompareNode : public NodeBase {
public:
  ThresholdsCompareNode(const NodeBaseConfig& base_config, const ThresholdsCompareNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(thresholds_compare, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(thresholds_compare, output, Output, std::vector<float>, "Output");

private:
  ThresholdsCompareNodeParams params_;
};

REGISTER_NODE(
  thresholds_compare,
  ThresholdsCompareNode,
  ThresholdsCompareNodeParams,
  "Compares input with thresholds and return 1 if value is higher and 0 if lower"
);

} // namespace giga_edge