#include "giga_edge/modules/audio/nodes/deinterleave/node.h"

#include "giga_edge/lib/audio/audio.h"

namespace giga_edge {

DeinterleaveNode::DeinterleaveNode(const NodeBaseConfig& base_config, const DeinterleaveNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void DeinterleaveNode::ProcessOnce() {
  PushAudio(DeinterleaveAudio(GetInterleavedAudio(), params_.num_channels));
}

} // namespace giga_edge
