#include "giga_edge/modules/common/nodes/logical_op/node.h"

#include "giga_edge/lib/logical/logical_op.h"

namespace giga_edge {

LogicalOpNode::LogicalOpNode(const NodeBaseConfig& base_config, const LogicalOpNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void LogicalOpNode::ProcessOnce() {
  const auto& first_input = GetFirstInput();
  const auto& second_input = GetSecondInput();

  if (second_input.has_value()) {
    PushOutput(LogicalOpBinary(first_input, second_input.value(), params_.logical_op_type));
  } else {
    PushOutput(LogicalOpUnary(first_input, params_.logical_op_type));
  }
}

} // namespace giga_edge
