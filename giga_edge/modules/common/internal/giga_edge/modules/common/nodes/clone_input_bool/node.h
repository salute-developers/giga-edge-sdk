#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {
class CloneInputBoolNode : public NodeBase {
public:
  explicit CloneInputBoolNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(clone_input_bool, input, Input, bool, "Input node");
  REGISTER_OUTPUT(clone_input_bool, first_output, FirstOutput, bool, "First output node");
  REGISTER_OUTPUT(clone_input_bool, second_output, SecondOutput, bool, "Second output node");
};

REGISTER_NODE(clone_input_bool, CloneInputBoolNode, "Clone bool input");
} // namespace giga_edge
