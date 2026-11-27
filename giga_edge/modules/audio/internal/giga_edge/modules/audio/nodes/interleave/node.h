#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class InterleaveNode : public NodeBase {
public:
  InterleaveNode(const NodeBaseConfig& base_config, const InterleaveNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(interleave, audio, Audio, std::vector<std::vector<float>>, "Channeled audio chunks");

  REGISTER_OUTPUT(interleave, interleaved_audio, InterleavedAudio, std::vector<float>, "Interleaved audio");

private:
  const InterleaveNodeParams params_;
};

REGISTER_NODE(interleave, InterleaveNode, InterleaveNodeParams, "Interleave channeled audio");

} // namespace giga_edge
