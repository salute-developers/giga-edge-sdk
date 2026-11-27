#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <vector>

namespace giga_edge {

struct AudioSegmenterParams {
  int sample_rate = 16000;
  int min_duration_ms = 20000;
  int max_duration_ms = 25000;
  int silence_duration_ms = 200;
  double silence_threshold_db = -20.0;
};

template<>
AudioSegmenterParams ParseConfig(const YAML::Node& config);

class AudioSegmenterNode : public NodeBase {
public:
  AudioSegmenterNode(const NodeBaseConfig& config, const AudioSegmenterParams& params);
  void ProcessOnce() override;
  void Reset() override;

  REGISTER_INPUT(audio_segmenter, audio, Audio, std::vector<float>, "Mono audio at sample_rate");
  REGISTER_OPTIONAL_INPUT(
    audio_segmenter,
    need_reset,
    NeedReset,
    bool,
    "Start a new utterance: buffered audio is dropped before this tick's audio and push are handled"
  );
  REGISTER_OPTIONAL_INPUT(audio_segmenter, push, Push, bool, "Flush the final segment");
  REGISTER_BATCH_OUTPUT(audio_segmenter, audio_batch, AudioBatch, std::vector<float>, "Contiguous audio segments");
  REGISTER_BATCH_OUTPUT(audio_segmenter, need_reset_batch, NeedResetBatch, bool, "Independent nonempty segment");

private:
  size_t SelectBoundary(size_t begin) const;
  void Emit(size_t begin, size_t count);
  size_t frame_samples_;
  size_t min_frames_;
  size_t max_frames_;
  size_t silence_frames_;
  double silence_ratio_;
  std::vector<float> buffered_;
};

REGISTER_NODE(
  audio_segmenter,
  AudioSegmenterNode,
  AudioSegmenterParams,
  "Split mono audio near quiet intervals without discarding samples; enforce a maximum duration"
);

} // namespace giga_edge
