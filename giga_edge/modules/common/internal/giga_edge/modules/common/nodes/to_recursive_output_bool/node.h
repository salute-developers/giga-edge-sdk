#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ToRecursiveOutputBoolNode : public NodeBase {
public:
  explicit ToRecursiveOutputBoolNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(to_recursive_output_bool, input, Input, bool, "The bool value to be made recursive");
  REGISTER_RECURSIVE_OUTPUT(to_recursive_output_bool, output, Output, bool, "Recursive bool output");
private:
  void DoInitRecursiveOutputs() override;
};

REGISTER_NODE(to_recursive_output_bool, ToRecursiveOutputBoolNode, "Node to make the bool value recursive");

} // namespace giga_edge