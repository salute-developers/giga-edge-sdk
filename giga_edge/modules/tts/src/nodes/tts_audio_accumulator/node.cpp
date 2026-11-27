#include "giga_edge/modules/tts/nodes/tts_audio_accumulator/node.h"

#include <utility>

namespace giga_edge {

TtsAudioAccumulatorNode::TtsAudioAccumulatorNode(
  const NodeBaseConfig& base_config, const TtsAudioAccumulatorParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void TtsAudioAccumulatorNode::ProcessOnce() {
  for (const auto* chunk : GetAudioChunkBatch()) {
    accumulated_.insert(accumulated_.end(), chunk->begin(), chunk->end());
  }

  const bool eos = GetIsEos();

  if (!params_.stream) {
    if (eos) {
      Emit(true);
    } else {
      PushAudio(SynthesizedAudio{.sample_rate = params_.sample_rate});
    }
    return;
  }

  const auto lead_samples = static_cast<size_t>(params_.min_lead_ms) * static_cast<size_t>(params_.sample_rate) / 1000;

  if (!streaming_started_ && accumulated_.size() < lead_samples && !eos) {
    PushAudio(SynthesizedAudio{.sample_rate = params_.sample_rate});
    return;
  }

  streaming_started_ = !eos;
  Emit(eos);
}

void TtsAudioAccumulatorNode::Emit(bool is_last) {
  PushAudio(
    SynthesizedAudio{
      .samples = std::exchange(accumulated_, {}),
      .sample_rate = params_.sample_rate,
      .is_last = is_last,
    }
  );
}

void TtsAudioAccumulatorNode::Reset() {
  accumulated_.clear();
  streaming_started_ = false;
}

} // namespace giga_edge
