#include "giga_edge/modules/common/nodes/data_gate/node.h"

namespace giga_edge {

DataGateNode::DataGateNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void DataGateNode::ProcessOnce() {
  const auto& input = GetInput();
  const auto& should_pass = GetGate();

  if (should_pass) {
    PushOutput(input);
  } else {
    PushOutput({});
  }
}

void DataGateNode::Reset() {}

} // namespace giga_edge
