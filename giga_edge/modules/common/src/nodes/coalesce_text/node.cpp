#include "giga_edge/modules/common/nodes/coalesce_text/node.h"

namespace giga_edge {

CoalesceTextNode::CoalesceTextNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void CoalesceTextNode::ProcessOnce() {
  const auto& first = GetFirst();
  PushText(first.has_value() && !first->get().empty() ? first->get() : GetSecond());
}

} // namespace giga_edge
