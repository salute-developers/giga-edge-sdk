#include "giga_edge/modules/common/nodes/clone_input_bool/node.h"

namespace giga_edge {

CloneInputBoolNode::CloneInputBoolNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void CloneInputBoolNode::ProcessOnce() {
  const auto& input = GetInput();

  PushFirstOutput(input);
  PushSecondOutput(input);
}

} // namespace giga_edge
