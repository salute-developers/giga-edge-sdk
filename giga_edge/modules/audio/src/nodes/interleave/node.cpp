#include "giga_edge/modules/audio/nodes/interleave/node.h"

#include "giga_edge/lib/audio/audio.h"

namespace giga_edge {

InterleaveNode::InterleaveNode(const NodeBaseConfig& base_config, const InterleaveNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void InterleaveNode::ProcessOnce() {
  PushInterleavedAudio(InterleaveAudio(GetAudio(), params_.channels));
}

} // namespace giga_edge
