#include "giga_edge/modules/enhance/nodes/denoiser/node.h"

namespace giga_edge {

DenoiserNode::DenoiserNode(const NodeBaseConfig& base_config, const DenoiserParams& params)
  : NodeBase(base_config)
  , params_(params)
  , denoiser_(std::make_shared<enhance::Denoiser>(params.config_yaml)) {
  uint32_t chunk_size = (params.frame_stride_ms * params.sampling_freq) / 1000u;
  processing_ = std::make_unique<ChunkBuffer>(denoiser_, chunk_size);
  denoiser_ctx_.get_deleter().sequencer = denoiser_.get();
  denoiser_ctx_.reset(denoiser_->CreateContext(params.sampling_freq, params.sampling_freq));
}

void DenoiserNode::ProcessOnce() {
  const auto audio = GetAudio();
  auto output = processing_->ProcessOnce(audio, denoiser_ctx_.get());
  PushAudio(std::move(output));
  if (HasSnrOutput()) {
    PushSnr(denoiser_->GetStreamStat(denoiser_ctx_.get()).snr);
  }
}

void DenoiserNode::Reset() {
  processing_->Reset();
  denoiser_ctx_.reset(denoiser_->CreateContext(params_.sampling_freq, params_.sampling_freq));
}

DenoiserNode::~DenoiserNode() = default;

} // namespace giga_edge
