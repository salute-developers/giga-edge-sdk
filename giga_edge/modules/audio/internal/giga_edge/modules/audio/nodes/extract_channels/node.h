#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ExtractChannelsNode : public NodeBase {
public:
  ExtractChannelsNode(const NodeBaseConfig& base_config, const ExtractChannelsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(extract_channels, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");

  REGISTER_OUTPUT(
    extract_channels, audio, Audio, std::vector<std::vector<float>>, "Channeled audio with extracted channels"
  );

private:
  const ExtractChannelsNodeParams params_;
};

REGISTER_NODE(
  extract_channels, ExtractChannelsNode, ExtractChannelsNodeParams, "Extracts multiple channel from channeled audio"
);

} // namespace giga_edge
