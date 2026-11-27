#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class IsEmptyNode : public NodeBase {
public:
  explicit IsEmptyNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_OPTIONAL_INPUT(is_empty, bool_vector, BoolVector, std::vector<bool>, "Vector of bools");
  REGISTER_OPTIONAL_INPUT(is_empty, float_vector, FloatVector, std::vector<float>, "Vector of floats");
  REGISTER_OPTIONAL_INPUT(is_empty, int_vector, IntVector, std::vector<int>, "Vector of ints");
  REGISTER_OPTIONAL_INPUT(is_empty, string, String, std::string, "String");
  REGISTER_OUTPUT(is_empty, output, Output, bool, "Shows if data is empty");
};

REGISTER_NODE(is_empty, IsEmptyNode, "Checks if data is empty");

} // namespace giga_edge
