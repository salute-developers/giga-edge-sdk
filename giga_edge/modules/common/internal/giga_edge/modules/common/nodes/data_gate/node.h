#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class DataGateNode : public NodeBase {
public:
  explicit DataGateNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override;

  REGISTER_INPUT(data_gate, input, Input, std::vector<float>, "Data to be gated");
  REGISTER_INPUT(data_gate, gate, Gate, bool, "Gate signal. True to pass data, false to block.");
  REGISTER_OUTPUT(data_gate, output, Output, std::vector<float>, "Gated data");
};

REGISTER_NODE(data_gate, DataGateNode, "Conditionally passes or blocks a data stream based on a boolean gate.");

} // namespace giga_edge
