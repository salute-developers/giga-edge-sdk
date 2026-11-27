#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ToFloatNode : public NodeBase {
public:
  ToFloatNode(const NodeBaseConfig& base_config, const ToFloatNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_OPTIONAL_INPUT(
    to_float,
    float_vector,
    FloatVector,
    std::vector<float>,
    "Vector with at most one float. If empty, result is `on_empty`"
  );
  REGISTER_OPTIONAL_INPUT(
    to_float, int_vector, IntVector, std::vector<int>, "Vector with at most one int. If empty, result is `on_empty`"
  );
  REGISTER_OPTIONAL_INPUT(to_float, int, Int, int, "Int");

  REGISTER_OPTIONAL_INPUT(to_float, bool, Bool, bool, "Bool");

  REGISTER_OUTPUT(to_float, output, Output, float, "Converted input");

private:
  ToFloatNodeParams params_;
};

REGISTER_NODE(
  to_float, ToFloatNode, ToFloatNodeParams, "Converts input to float. Only one of data should be specified"
);

} // namespace giga_edge