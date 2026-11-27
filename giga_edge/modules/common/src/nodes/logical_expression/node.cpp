#include "giga_edge/modules/common/nodes/logical_expression/node.h"

namespace giga_edge {

LogicalExpressionNode::LogicalExpressionNode(
  const NodeBaseConfig& base_config, const LogicalExpressionNodeParams& params
)
  : NodeBase(base_config)
  , expression_(ParseLogicalExpression(params.expression)) {
  inputs_ = expression_->Variables();
}

void LogicalExpressionNode::ProcessOnce() {
  std::unordered_map<std::string, bool> variables;
  for (const auto& input : inputs_) {
    variables[std::string{input}] = inputs_data_->GetSingle<bool>(std::string{input});
  }
  PushResult(expression_->Apply(variables));
}

std::unordered_set<std::string> LogicalExpressionNode::GetDynamicInputs() {
  std::unordered_set<std::string> inputs;
  for (auto& input : inputs_) {
    inputs.insert(std::string{input});
  }
  return inputs;
}

} // namespace giga_edge
