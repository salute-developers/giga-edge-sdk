#include "giga_edge/modules/common/nodes/math_expression/node.h"

namespace giga_edge {

MathExpressionNode::MathExpressionNode(const NodeBaseConfig& base_config, const MathExpressionNodeParams& params)
  : NodeBase(base_config)
  , expression_(ParseMathExpression(params.expression)) {
  inputs_ = expression_->Variables();
}

void MathExpressionNode::ProcessOnce() {
  std::unordered_map<std::string, float> variables;
  for (const auto& input : inputs_) {
    variables[std::string{input}] = inputs_data_->GetSingle<float>(std::string{input});
  }
  PushResult(expression_->Apply(variables));
}

std::unordered_set<std::string> MathExpressionNode::GetDynamicInputs() {
  std::unordered_set<std::string> inputs;
  for (auto& input : inputs_) {
    inputs.insert(std::string{input});
  }
  return inputs;
}

} // namespace giga_edge
