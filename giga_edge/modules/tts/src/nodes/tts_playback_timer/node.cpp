#include "giga_edge/modules/tts/nodes/tts_playback_timer/node.h"

#include <algorithm>

namespace giga_edge {

TtsPlaybackTimerNode::TtsPlaybackTimerNode(const NodeBaseConfig& base_config, const TtsPlaybackTimerParams& params)
  : NodeBase(base_config)
  , params_(params)
  , sounding_ms_(-params.playing_tail_ms) {}

void TtsPlaybackTimerNode::ProcessOnce() {
  const auto& audio = GetAudio();
  const double playing_tail_ms = params_.playing_tail_ms;
  sounding_ms_ = std::max(sounding_ms_ - params_.tick_ms, -playing_tail_ms);
  if (audio.sample_rate > 0 && !audio.samples.empty()) {
    const double length_ms = 1000.0 * static_cast<double>(audio.samples.size()) / audio.sample_rate;
    left_ms_ += length_ms;
    // The client plays the chunk after the audio it has not played yet, or at once if that has ended.
    sounding_ms_ = std::max(sounding_ms_, 0.0) + length_ms;
  }
  if (audio.is_last) {
    ending_ = true;
    left_ms_ += params_.tail_ms;
  }
  // One tick of the graph is tick_ms of the sound: that much of what is left has been played by the next tick.
  left_ms_ = std::max(0.0, left_ms_ - params_.tick_ms);
  const bool played = ending_ && left_ms_ <= 0;
  if (played) {
    ending_ = false;
  }
  PushPlayed(played);
  PushPlaying(sounding_ms_ > -playing_tail_ms);
}

void TtsPlaybackTimerNode::Reset() {
  left_ms_ = 0;
  ending_ = false;
  sounding_ms_ = -params_.playing_tail_ms;
}

} // namespace giga_edge
