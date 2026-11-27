#include "giga_edge/modules/common/nodes/to_float/node.h"

#include <numeric>

namespace giga_edge {

ToFloatNode::ToFloatNode(const NodeBaseConfig& base_config, const ToFloatNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  if (base_config.inputs.size() != 1) {
    throw std::runtime_error("ToFloatNode: exactly one of data inputs should be present");
  }
}

void ToFloatNode::ProcessOnce() {
  if (const auto& input = GetIntVector(); input.has_value()) {
    const auto& data = input.value().get();
    if (data.size() > 1) {
      throw std::runtime_error("ToFloatNode: Got int_vector input with more than one element");
    }
    PushOutput(data.empty() ? params_.on_empty : data.front());
  }

  if (const auto& input = GetFloatVector(); input.has_value()) {
    const auto& data = input.value().get();
    if (data.size() > 1) {
      throw std::runtime_error("ToFloatNode: Got float_vector input with more than one element");
    }
    PushOutput(data.empty() ? params_.on_empty : data.front());
  }

  if (const auto& input = GetInt(); input.has_value()) {
    PushOutput(input.value().get());
  }

  if (const auto& input = GetBool(); input.has_value()) {
    PushOutput(input.value().get());
  }
}

} // namespace giga_edge