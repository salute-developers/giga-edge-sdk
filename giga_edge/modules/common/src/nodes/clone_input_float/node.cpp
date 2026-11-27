#include "giga_edge/modules/common/nodes/clone_input_float/node.h"

namespace giga_edge {

CloneInputFloatNode::CloneInputFloatNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void CloneInputFloatNode::ProcessOnce() {
  const auto& input = GetInput();

  PushFirstOutput(input);
  PushSecondOutput(input);
}

} // namespace giga_edge
