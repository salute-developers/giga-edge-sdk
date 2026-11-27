#include "giga_edge/modules/common/nodes/forward_accumulated_buffer/node.h"

namespace giga_edge {

ForwardAccumulatedBufferNode::ForwardAccumulatedBufferNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void ForwardAccumulatedBufferNode::ProcessOnce() {
  const auto& input = GetInput();
  bool is_buffer_changed = !input.empty();
  const auto& drop = GetDrop();
  if (drop.has_value() && drop.value()) {
    is_buffer_changed |= !data_.empty();
    data_.clear();
  }

  data_.insert(data_.end(), input.begin(), input.end());
  PushOutput(data_);
  PushIsBufferChanged(is_buffer_changed);
}

void ForwardAccumulatedBufferNode::Reset() {
  data_.clear();
}

} // namespace giga_edge
