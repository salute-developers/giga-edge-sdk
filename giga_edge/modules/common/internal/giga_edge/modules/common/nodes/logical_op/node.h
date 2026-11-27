#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class LogicalOpNode : public NodeBase {
public:
  LogicalOpNode(const NodeBaseConfig& base_config, const LogicalOpNodeParams& logical_node_params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(
    logical_op,
    first_input,
    FirstInput,
    bool,
    "First input for logic operation\
                                                            (can be single if it is unary logic operation)"
  );
  REGISTER_OPTIONAL_INPUT(
    logical_op,
    second_input,
    SecondInput,
    bool,
    "Second input for logic operation\
                                                                      (optional)"
  );

  REGISTER_OUTPUT(logical_op, output, Output, bool, "Logical op result");

private:
  LogicalOpNodeParams params_;
};

REGISTER_NODE(
  logical_op,
  LogicalOpNode,
  LogicalOpNodeParams,
  "Node for a logical operation (the type is specified in the parameters)"
);

} // namespace giga_edge
