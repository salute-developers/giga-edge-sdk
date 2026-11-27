#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/enhance/chunk_buffer.h"
#include "giga_edge/modules/enhance/nodes/echo_cancelation/params.h"

#include <deque>
#include <vector>

namespace giga_edge {

class EchoCancelationNode : public NodeBase {
public:
  EchoCancelationNode(const NodeBaseConfig& base_config, const EchoCancelationParams& aec_params);

  void ProcessOnce() override;

  void Reset() override;

  ~EchoCancelationNode() override;

  REGISTER_INPUT(
    enhance_echo_cancelation, interleaved_audio, Audio, std::vector<float>, "Incoming audio (2 channel interleaved)"
  );
  REGISTER_OUTPUT(
    enhance_echo_cancelation, audio, Audio, std::vector<float>, "Audio after echo cancelation processing"
  );

private:
  EchoCancelationParams params_;
  std::shared_ptr<enhance::EchoCanceller> aec_;
  std::unique_ptr<ChunkBuffer> processing_;
};

REGISTER_NODE(
  enhance_echo_cancelation, EchoCancelationNode, EchoCancelationParams, "Audio echo cancelation processing"
);

} // namespace giga_edge
