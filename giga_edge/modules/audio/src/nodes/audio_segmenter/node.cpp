#include "giga_edge/modules/audio/nodes/audio_segmenter/node.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace giga_edge {

template<>
AudioSegmenterParams ParseConfig(const YAML::Node& config) {
  AudioSegmenterParams result;
  result.sample_rate = config["sample_rate"].as<int>(result.sample_rate);
  result.min_duration_ms = config["min_duration_ms"].as<int>(result.min_duration_ms);
  result.max_duration_ms = config["max_duration_ms"].as<int>(result.max_duration_ms);
  result.silence_duration_ms = config["silence_duration_ms"].as<int>(result.silence_duration_ms);
  result.silence_threshold_db = config["silence_threshold_db"].as<double>(result.silence_threshold_db);
  return result;
}

AudioSegmenterNode::AudioSegmenterNode(const NodeBaseConfig& config, const AudioSegmenterParams& params)
  : NodeBase(config) {
  if (
    params.sample_rate < 100 || params.min_duration_ms <= 0 || params.max_duration_ms < params.min_duration_ms
    || params.silence_duration_ms <= 0 || params.silence_duration_ms > params.max_duration_ms
    || params.min_duration_ms % 10 != 0 || params.max_duration_ms % 10 != 0 || params.silence_duration_ms % 10 != 0
    || !std::isfinite(params.silence_threshold_db) || params.silence_threshold_db >= 0
  ) {
    throw std::invalid_argument("audio_segmenter: invalid rate, durations or relative silence threshold");
  }
  frame_samples_ = static_cast<size_t>(params.sample_rate / 100);
  min_frames_ = static_cast<size_t>(params.min_duration_ms / 10);
  max_frames_ = static_cast<size_t>(params.max_duration_ms / 10);
  silence_frames_ = static_cast<size_t>(params.silence_duration_ms / 10);
  silence_ratio_ = std::pow(10.0, params.silence_threshold_db / 10.0);
}

size_t AudioSegmenterNode::SelectBoundary(size_t begin) const {
  const size_t maximum = max_frames_ * frame_samples_;
  if (min_frames_ + silence_frames_ > max_frames_) {
    return maximum;
  }
  std::vector<double> energy(max_frames_);
  for (size_t frame = 0; frame < max_frames_; ++frame) {
    double sum = 0;
    for (size_t sample = 0; sample < frame_samples_; ++sample) {
      const double value = buffered_[begin + frame * frame_samples_ + sample];
      sum += value * value;
    }
    energy[frame] = sum / frame_samples_;
  }
  double window = 0;
  for (size_t i = 0; i < silence_frames_; ++i) {
    window += energy[i];
  }
  double peak = 0;
  double quietest = std::numeric_limits<double>::infinity();
  size_t boundary = max_frames_;
  for (size_t start = 0; start + silence_frames_ <= max_frames_; ++start) {
    peak = std::max(peak, window);
    if (start >= min_frames_ && window < quietest) {
      quietest = window;
      boundary = start + silence_frames_ / 2;
    }
    if (start + silence_frames_ < max_frames_) {
      window = std::max(0.0, window + energy[start + silence_frames_] - energy[start]);
    }
  }
  return peak > 0 && quietest <= peak * silence_ratio_ ? boundary * frame_samples_ : maximum;
}

void AudioSegmenterNode::Emit(size_t begin, size_t count) {
  PushAudioBatch(std::vector<float>(buffered_.begin() + begin, buffered_.begin() + begin + count));
  PushNeedResetBatch(count != 0);
}

void AudioSegmenterNode::ProcessOnce() {
  if (const auto reset = GetNeedReset(); reset.has_value() && reset.value().get()) {
    Reset();
  }
  const auto& audio = GetAudio();
  buffered_.insert(buffered_.end(), audio.begin(), audio.end());
  size_t begin = 0;
  bool emitted = false;
  const auto push = GetPush();
  const bool flush = push.has_value() && push.value().get();
  while (buffered_.size() - begin >= max_frames_ * frame_samples_) {
    const size_t count = SelectBoundary(begin);
    Emit(begin, count);
    begin += count;
    emitted = true;
  }
  if (flush && begin < buffered_.size()) {
    Emit(begin, buffered_.size() - begin);
    begin = buffered_.size();
    emitted = true;
  }
  if (!emitted) {
    Emit(begin, 0);
  }
  buffered_.erase(buffered_.begin(), buffered_.begin() + begin);
}

void AudioSegmenterNode::Reset() {
  buffered_.clear();
}

} // namespace giga_edge
