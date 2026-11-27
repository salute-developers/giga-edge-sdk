#pragma once

#include "params.h"

#include "giga_edge/lib/math/math_expression.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class MathExpressionNode : public NodeBase {
public:
  MathExpressionNode(const NodeBaseConfig& base_config, const MathExpressionNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_DYNAMIC_INPUTS(math_expression);
  REGISTER_OUTPUT(math_expression, result, Result, float, "Math expression result");

private:
  IMathExpressionPtr expression_;

  std::unordered_set<std::string_view> inputs_;
};

REGISTER_NODE(math_expression, MathExpressionNode, MathExpressionNodeParams, "Node for calculating math expression");

} // namespace giga_edge
