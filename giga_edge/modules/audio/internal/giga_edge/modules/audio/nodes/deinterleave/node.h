#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class DeinterleaveNode : public NodeBase {
public:
  DeinterleaveNode(const NodeBaseConfig& base_config, const DeinterleaveNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(deinterleave, interleaved_audio, InterleavedAudio, std::vector<float>, "Interleaved audio");

  REGISTER_OUTPUT(deinterleave, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");

private:
  DeinterleaveNodeParams params_;
};

REGISTER_NODE(deinterleave, DeinterleaveNode, DeinterleaveNodeParams, "Deinterleave interleaved audio");

} // namespace giga_edge
