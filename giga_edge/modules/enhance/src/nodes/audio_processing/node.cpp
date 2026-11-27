#include "giga_edge/modules/enhance/nodes/audio_processing/node.h"

namespace giga_edge {

AudioProcessingNode::AudioProcessingNode(const NodeBaseConfig& base_config, const AudioProcessingParams& params)
  : NodeBase(base_config)
  , audio_process_(std::make_unique<enhance::AudioProcessor>()) {
  uint32_t freq = params.sampling_freq;
  // The stages run in this order: LC + DRC + NG + Limiter in one node is the whole normalization before the denoiser.

  // Loudness Control
  if (params.lc_enabled) {
    audio_process_->AddLC(
      freq,
      params.lc_target_level_db,
      params.lc_bypass_time_sec,
      params.lc_max_gain_db,
      params.lc_use_vad,
      params.lc_post_processing
    );
  }

  // Dynamic Range Compressor
  if (params.drc_enabled) {
    audio_process_->AddDRC(
      freq,
      params.drc_threshold_db,
      params.drc_ratio,
      params.drc_knee_width,
      params.drc_attack_time,
      params.drc_release_time
    );
  }

  // Noise Gate
  if (params.ng_enabled) {
    audio_process_->AddNG(
      freq, params.ng_threshold_db, params.ng_attack_time, params.ng_release_time, params.ng_hold_time
    );
  }

  // Limiter
  if (params.lim_enabled) {
    audio_process_->AddLim(
      freq, params.lim_threshold_db, params.lim_knee_width, params.lim_attack_time, params.lim_release_time
    );
  }

  // Range Control
  if (params.rc_enabled) {
    audio_process_->AddRC(
      freq,
      params.rc_target_level_db,
      params.rc_max_gain_db,
      params.rc_attack_time,
      params.rc_release_time,
      params.rc_hold_time
    );
  }

  // Automatic Gain Control
  if (params.agc_enabled) {
    audio_process_->AddAGC(
      freq, params.agc_target_power_level, params.agc_max_power_gain, params.agc_time_rise, params.agc_time_fall
    );
  }
}

void AudioProcessingNode::ProcessOnce() {
  const auto& audio = GetAudio();
  std::vector<float> output;
  audio_process_->ProcessChunk(audio, output);
  PushAudio(std::move(output));
}

void AudioProcessingNode::Reset() {}

AudioProcessingNode::~AudioProcessingNode() = default;

} // namespace giga_edge
