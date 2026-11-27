#include "giga_edge/modules/common/nodes/to_bool/node.h"

#include <numeric>

namespace giga_edge {

ToBoolNode::ToBoolNode(const NodeBaseConfig& base_config, const ToBoolNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  int n_data_inputs = 0;
  for (const auto& [input_name, _] : base_config.inputs) {
    if (input_name != "threshold") {
      ++n_data_inputs;
    }
  }

  if (n_data_inputs != 1) {
    throw std::runtime_error("ToBoolNode: exactly one of data inputs should be present");
  }
}

void ToBoolNode::ProcessOnce() {
  const auto& threshold_input = GetThreshold();
  float threshold = threshold_input.has_value() ? threshold_input.value().get() : params_.threshold;

  if (const auto& input = GetIntVector(); input.has_value()) {
    const auto& data = input.value().get();
    if (data.size() > 1) {
      throw std::runtime_error("ToBoolNode: Got int_vector input with more than one element");
    }
    PushOutput(data.empty() ? false : data.front() >= threshold);
  }

  if (const auto& input = GetBoolVector(); input.has_value()) {
    const auto& data = input.value().get();
    if (data.size() > 1) {
      throw std::runtime_error("ToBoolNode: Got bool_vector input with more than one element");
    }
    PushOutput(data.empty() ? false : data.front());
  }

  if (const auto& input = GetString(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(data.empty() ? false : true);
  }

  if (const auto& input = GetFloatVector(); input.has_value()) {
    const auto& data = input.value().get();
    if (data.size() > 1) {
      throw std::runtime_error("ToBoolNode: Got float_vector input with more than one element");
    }
    PushOutput(data.empty() ? false : data.front() >= threshold);
  }

  if (const auto& input = GetInt(); input.has_value()) {
    PushOutput(input.value().get() >= threshold);
  }

  if (const auto& input = GetFloat(); input.has_value()) {
    PushOutput(input.value().get() >= threshold);
  }
}

} // namespace giga_edge