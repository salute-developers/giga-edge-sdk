#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ConcatVecFloatNode : public NodeBase {
public:
  explicit ConcatVecFloatNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_DYNAMIC_INPUTS(concat_vec_float);
  REGISTER_OUTPUT(concat_vec_float, output, Output, std::vector<float>, "Concatenated float vectors")

private:
  std::vector<std::string> input_names_;
};

REGISTER_NODE(concat_vec_float, ConcatVecFloatNode, "Concatenates float vectors from different inputs");

} // namespace giga_edge
