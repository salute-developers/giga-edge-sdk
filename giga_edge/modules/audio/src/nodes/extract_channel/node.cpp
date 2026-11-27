#include "giga_edge/modules/audio/nodes/extract_channel/node.h"

namespace giga_edge {

ExtractChannelNode::ExtractChannelNode(const NodeBaseConfig& base_config, const ExtractChannelNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void ExtractChannelNode::ProcessOnce() {
  PushChannel(GetAudio()[params_.channel]);
}

} // namespace giga_edge
