#include "giga_edge/modules/common/nodes/one_time_padding/node.h"

#include "giga_edge/lib/padding/padding.h"

namespace giga_edge {

OneTimePaddingNode::OneTimePaddingNode(const NodeBaseConfig& base_config, const PaddingParams& params)
  : NodeBase(base_config)
  , params_(params)
  , activated_(false) {}

void OneTimePaddingNode::ProcessOnce() {
  if (const auto reset = GetNeedReset(); reset.has_value() && reset.value().get()) {
    activated_ = false;
  }
  auto input = GetInput();
  if (!activated_) {
    ApplyPadding(input, params_);
    activated_ = true;
  }
  PushOutput(input);
}

void OneTimePaddingNode::Reset() {
  activated_ = false;
}

} // namespace giga_edge
