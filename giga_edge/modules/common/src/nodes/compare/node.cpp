#include "giga_edge/modules/common/nodes/compare/node.h"

#include "giga_edge/lib/compare/compare.h"

namespace giga_edge {

CompareOpNode::CompareOpNode(const NodeBaseConfig& base_config, const CompareOpNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void CompareOpNode::ProcessOnce() {
  const auto& first_input = GetFirstInput();
  const auto& second_input = GetSecondInput();

  PushOutput(CompareOp(first_input, second_input, params_.compare_op_type));
}

} // namespace giga_edge
