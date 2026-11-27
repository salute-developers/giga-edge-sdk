#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <vector>

namespace giga_edge {

class ToVecFloatNode : public NodeBase {
public:
  ToVecFloatNode(const NodeBaseConfig& base_config, const ToVecFloatNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_OPTIONAL_INPUT(
    to_vec_float,
    float,
    Float,
    float,
    "Float. Converted to a one-element vector, or to an empty vector if equal to `empty_value`"
  );
  REGISTER_OPTIONAL_INPUT(
    to_vec_float,
    int,
    Int,
    int,
    "Int. Converted to a one-element vector, or to an empty vector if equal to `empty_value`"
  );
  REGISTER_OPTIONAL_INPUT(
    to_vec_float,
    bool,
    Bool,
    bool,
    "Bool (false -> 0, true -> 1). Converted to a one-element vector, or to an empty vector if equal to `empty_value`"
  );
  REGISTER_OPTIONAL_INPUT(
    to_vec_float, int_vector, IntVector, std::vector<int>, "Vector of ints, converted elementwise"
  );
  REGISTER_OPTIONAL_INPUT(
    to_vec_float, bool_vector, BoolVector, std::vector<bool>, "Vector of bools, converted elementwise"
  );

  REGISTER_OUTPUT(to_vec_float, output, Output, std::vector<float>, "Converted input");

private:
  void PushScalar(float value) const;

  ToVecFloatNodeParams params_;
};

REGISTER_NODE(
  to_vec_float,
  ToVecFloatNode,
  ToVecFloatNodeParams,
  "Converts input to a vector of floats. Only one of data should be specified. "
  "A scalar equal to `empty_value` (unset by default) is converted to an empty vector"
);

} // namespace giga_edge
