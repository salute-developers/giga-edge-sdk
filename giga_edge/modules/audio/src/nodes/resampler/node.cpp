#include "giga_edge/modules/audio/nodes/resampler/node.h"

#include "dsplib.h"

namespace giga_edge {

ResamplerNode::ResamplerNode(const NodeBaseConfig& base_config, const ResamplerParams& params)
  : NodeBase(base_config)
  , params_(params)
  , resampler_(std::make_unique<dsplib::FIRResampler>(params.to_freq, params.from_freq)) {}

void ResamplerNode::ProcessOnce() {
  const auto& audio = GetAudio();
  leftover_samples_.insert(leftover_samples_.end(), audio.begin(), audio.end());
  // dsplib throws unless the input length is a multiple of the decimation factor
  const auto ready = resampler_->prev_size(static_cast<int>(leftover_samples_.size()));
  auto out = resampler_->process(std::vector<float>(leftover_samples_.begin(), leftover_samples_.begin() + ready));
  leftover_samples_.erase(leftover_samples_.begin(), leftover_samples_.begin() + ready);
  PushAudio(out.to_vec());
}

void ResamplerNode::Reset() {
  leftover_samples_.clear();
  resampler_ = std::make_unique<dsplib::FIRResampler>(params_.to_freq, params_.from_freq);
}

ResamplerNode::~ResamplerNode() = default;
} // namespace giga_edge