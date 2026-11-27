#include "giga_edge/modules/common/nodes/accumulate_text/node.h"

#include <utility>

namespace giga_edge {

AccumulateTextNode::AccumulateTextNode(const NodeBaseConfig& base_config, const AccumulateTextNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void AccumulateTextNode::ProcessOnce() {
  const auto& restart = GetRestart();
  if (restart.has_value() && restart.value().get()) {
    Reset();
  }

  const auto& input = GetInput();

  if (!input.empty()) {
    data_ += input;
  }

  const auto& drop = GetDrop();
  if (drop.has_value() && drop.value().get()) {
    Reset();
  }

  if (GetPush()) {
    PushOutput(params_.clear_after_push ? std::exchange(data_, {}) : data_);
    return;
  }

  PushOutput("");
}

void AccumulateTextNode::Reset() {
  data_.clear();
}

} // namespace giga_edge
