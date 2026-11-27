#include "giga_edge/modules/common/nodes/reset_clock/node.h"

namespace giga_edge {

ResetClockNode::ResetClockNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void ResetClockNode::ProcessOnce() {
  PushSinceReset(since_last_reset_++);
}

void ResetClockNode::Reset() {
  since_last_reset_ = 0;
}

std::unordered_set<std::string> ResetClockNode::GetDynamicInputs() {
  std::unordered_set<std::string> inputs;
  for (const auto& input : Inputs()) {
    inputs.insert(input.first);
  }
  return inputs;
}

} // namespace giga_edge
