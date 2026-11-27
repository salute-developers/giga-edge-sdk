#include "giga_edge/modules/common/nodes/to_recursive_output_bool/node.h"

namespace giga_edge {

ToRecursiveOutputBoolNode::ToRecursiveOutputBoolNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void ToRecursiveOutputBoolNode::DoInitRecursiveOutputs() {
  PushOutput(false);
}

void ToRecursiveOutputBoolNode::ProcessOnce() {
  const auto& input = GetInput();

  PushOutput(input);
}

} // namespace giga_edge
