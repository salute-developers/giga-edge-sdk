#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class LengthNode : public NodeBase {
public:
  explicit LengthNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_OPTIONAL_INPUT(length, float_vector, FloatVector, std::vector<float>, "Vector of floats");
  REGISTER_OPTIONAL_INPUT(
    length, string, String, std::string, "String; its length is measured in UTF-8 codepoints, not bytes"
  );
  REGISTER_OUTPUT(length, length, Length, float, "Length of the input data");
};

REGISTER_NODE(length, LengthNode, "Calculates length of the input data");

} // namespace giga_edge
