#include "giga_edge/modules/common/nodes/to_vec_float/node.h"

#include <stdexcept>

namespace giga_edge {

ToVecFloatNode::ToVecFloatNode(const NodeBaseConfig& base_config, const ToVecFloatNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  if (base_config.inputs.size() != 1) {
    throw std::runtime_error("ToVecFloatNode: exactly one of data inputs should be present");
  }
}

void ToVecFloatNode::ProcessOnce() {
  if (const auto& input = GetFloat(); input.has_value()) {
    PushScalar(input.value().get());
  }

  if (const auto& input = GetInt(); input.has_value()) {
    PushScalar(static_cast<float>(input.value().get()));
  }

  if (const auto& input = GetBool(); input.has_value()) {
    PushScalar(input.value().get() ? 1.0f : 0.0f);
  }

  if (const auto& input = GetIntVector(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(std::vector<float>(data.begin(), data.end()));
  }

  if (const auto& input = GetBoolVector(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(std::vector<float>(data.begin(), data.end()));
  }
}

void ToVecFloatNode::PushScalar(float value) const {
  if (params_.empty_value.has_value() && params_.empty_value.value() == value) {
    PushOutput({});
  } else {
    PushOutput({value});
  }
}

} // namespace giga_edge
