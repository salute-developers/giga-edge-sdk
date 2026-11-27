#include "giga_edge/modules/common/nodes/is_empty/node.h"

namespace giga_edge {

IsEmptyNode::IsEmptyNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  if (base_config.inputs.size() != 1) {
    throw std::runtime_error("IsEmptyNode: exactly one of data inputs should be present");
  }
}

void IsEmptyNode::ProcessOnce() {
  if (const auto& input = GetBoolVector(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(data.empty());
  }

  if (const auto& input = GetFloatVector(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(data.empty());
  }

  if (const auto& input = GetIntVector(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(data.empty());
  }

  if (const auto& input = GetString(); input.has_value()) {
    const auto& data = input.value().get();
    PushOutput(data.empty());
  }
}

} // namespace giga_edge