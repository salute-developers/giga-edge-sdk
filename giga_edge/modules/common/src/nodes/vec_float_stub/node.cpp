#include "giga_edge/modules/common/nodes/vec_float_stub/node.h"

namespace giga_edge {

VecFloatStubNode::VecFloatStubNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void VecFloatStubNode::ProcessOnce() {
  PushVecFloat({});
}

} // namespace giga_edge
