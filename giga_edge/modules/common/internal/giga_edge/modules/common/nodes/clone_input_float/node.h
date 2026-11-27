#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {
class CloneInputFloatNode : public NodeBase {
public:
  explicit CloneInputFloatNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(clone_input_float, input, Input, std::vector<float>, "Input node");
  REGISTER_OUTPUT(clone_input_float, first_output, FirstOutput, std::vector<float>, "First output node");
  REGISTER_OUTPUT(clone_input_float, second_output, SecondOutput, std::vector<float>, "Second output node");
};

REGISTER_NODE(clone_input_float, CloneInputFloatNode, "Clone float input");
} // namespace giga_edge
