#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class CompareOpNode : public NodeBase {
public:
  CompareOpNode(const NodeBaseConfig& base_config, const CompareOpNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(compare_op, first_input, FirstInput, float, "First input to comparison");
  REGISTER_INPUT(compare_op, second_input, SecondInput, float, "Second input for comparison");

  REGISTER_OUTPUT(compare_op, output, Output, bool, "Comparison result according to copmare_op_type");

private:
  CompareOpNodeParams params_;
};

REGISTER_NODE(
  compare_op,
  CompareOpNode,
  CompareOpNodeParams,
  "Node for performing the operation of comparing\
                                                              two inputs (the type of comparison is set through the parameters)"
);

} // namespace giga_edge
