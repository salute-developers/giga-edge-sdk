#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <string>

namespace giga_edge {

class ToBoolNode : public NodeBase {
public:
  ToBoolNode(const NodeBaseConfig& base_config, const ToBoolNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_OPTIONAL_INPUT(
    to_bool, float_vector, FloatVector, std::vector<float>, "Vector with at most one float. If empty, result is false"
  );
  REGISTER_OPTIONAL_INPUT(
    to_bool, int_vector, IntVector, std::vector<int>, "Vector with at most one int. If empty, result is false"
  );

  REGISTER_OPTIONAL_INPUT(
    to_bool, bool_vector, BoolVector, std::vector<bool>, "Vector with at most one bool. If empty, result is false"
  );

  REGISTER_OPTIONAL_INPUT(to_bool, float, Float, float, "Float");
  REGISTER_OPTIONAL_INPUT(to_bool, int, Int, int, "Int");
  REGISTER_OPTIONAL_INPUT(to_bool, string, String, std::string, "String to check for its non-emptiness");

  REGISTER_OPTIONAL_INPUT(to_bool, threshold, Threshold, float, "threshold");

  REGISTER_OUTPUT(to_bool, output, Output, bool, "Converted input");

private:
  ToBoolNodeParams params_;
};

REGISTER_NODE(
  to_bool,
  ToBoolNode,
  ToBoolNodeParams,
  "Checks if input >= threshold. Only one of data should be specified.\
                                          For a string, it is checked for its non-emptiness."
);

} // namespace giga_edge