#include "giga_edge/modules/common/nodes/padding/node.h"

#include "giga_edge/lib/padding/padding.h"

namespace giga_edge {

PaddingNode::PaddingNode(const NodeBaseConfig& base_config, const PaddingParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void PaddingNode::ProcessOnce() {
  auto input = GetInput();
  if (!input.empty()) {
    ApplyPadding(input, params_);
  }
  PushOutput(input);
}

void PaddingNode::Reset() {}

} // namespace giga_edge
