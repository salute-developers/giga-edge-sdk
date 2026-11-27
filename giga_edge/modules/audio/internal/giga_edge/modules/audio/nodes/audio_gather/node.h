#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class AudioGatherNode : public NodeBase {
public:
  explicit AudioGatherNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_BATCH_INPUT(audio_gather, audio, Audio, std::vector<std::vector<float>>, "Channeled audio chunks");

  REGISTER_OUTPUT(audio_gather, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");
};

REGISTER_NODE(audio_gather, AudioGatherNode, "Gather channeled audio");

} // namespace giga_edge
