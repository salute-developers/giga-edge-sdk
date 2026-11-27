#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class CombineChannelsNode : public NodeBase {
public:
  explicit CombineChannelsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_DYNAMIC_INPUTS(combine_channels);

  REGISTER_OUTPUT(combine_channels, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");

private:
  std::vector<std::string> input_names_;
};

REGISTER_NODE(combine_channels, CombineChannelsNode, "Combine single channel audio into channeled audio");

} // namespace giga_edge
