#pragma once

#include "params.h"

#include "giga_edge/lib/logical/logical_expression.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class LogicalExpressionNode : public NodeBase {
public:
  LogicalExpressionNode(const NodeBaseConfig& base_config, const LogicalExpressionNodeParams& logical_node_params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_DYNAMIC_INPUTS(logical_expression);
  REGISTER_OUTPUT(logical_expression, result, Result, bool, "Logical expression result");

private:
  ILogicalExpressionPtr expression_;

  std::unordered_set<std::string_view> inputs_;
};

REGISTER_NODE(
  logical_expression, LogicalExpressionNode, LogicalExpressionNodeParams, "Node for calculating logical expression"
);

} // namespace giga_edge
