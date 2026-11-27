#include "giga_edge/modules/audio/nodes/extract_channels/node.h"

namespace giga_edge {

ExtractChannelsNode::ExtractChannelsNode(const NodeBaseConfig& base_config, const ExtractChannelsNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void ExtractChannelsNode::ProcessOnce() {
  const auto& audio = GetAudio();

  std::vector<std::vector<float>> result;
  result.reserve(params_.channels.size());

  for (const auto& channel : params_.channels) {
    result.push_back(audio[channel]);
  }
  PushAudio(result);
}

} // namespace giga_edge
