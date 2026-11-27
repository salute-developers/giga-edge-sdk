#include "giga_edge/modules/enhance/nodes/echo_cancelation/node.h"

namespace giga_edge {

EchoCancelationNode::EchoCancelationNode(const NodeBaseConfig& base_config, const EchoCancelationParams& params)
  : NodeBase(base_config)
  , params_(params)
  , aec_(std::make_shared<enhance::EchoCanceller>(params.config_yaml)) {
  processing_ = std::make_unique<ChunkBuffer>(aec_, params.frame_len);
}

void EchoCancelationNode::ProcessOnce() {
  const auto interleaved_audio = GetAudio();
  auto output = processing_->ProcessOnce(interleaved_audio, nullptr);
  PushAudio(std::move(output));
}

void EchoCancelationNode::Reset() {
  processing_->Reset();
}

EchoCancelationNode::~EchoCancelationNode() = default;

} // namespace giga_edge
