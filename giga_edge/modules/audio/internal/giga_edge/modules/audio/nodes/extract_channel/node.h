#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ExtractChannelNode : public NodeBase {
public:
  ExtractChannelNode(const NodeBaseConfig& base_config, const ExtractChannelNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(extract_channel, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");

  REGISTER_OUTPUT(extract_channel, channel, Channel, std::vector<float>, "Single audio channel");

private:
  const ExtractChannelNodeParams params_;
};

REGISTER_NODE(
  extract_channel, ExtractChannelNode, ExtractChannelNodeParams, "Extracts single channel from channeled audio"
);

} // namespace giga_edge
