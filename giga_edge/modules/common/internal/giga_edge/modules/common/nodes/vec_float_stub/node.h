#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class VecFloatStubNode : public NodeBase {
public:
  explicit VecFloatStubNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(vec_float_stub, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(vec_float_stub, vec_float, VecFloat, std::vector<float>, "Empty vec_float vector");
};

REGISTER_NODE(vec_float_stub, VecFloatStubNode, "Stub, that generates empty vector<float>");

} // namespace giga_edge
