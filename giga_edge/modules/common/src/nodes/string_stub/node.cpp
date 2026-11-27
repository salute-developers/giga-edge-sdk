#include "giga_edge/modules/common/nodes/string_stub/node.h"

namespace giga_edge {

StringStubNode::StringStubNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void StringStubNode::ProcessOnce() {
  PushText({});
}

} // namespace giga_edge
