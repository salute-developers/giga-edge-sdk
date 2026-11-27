#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/tts/synthesized_audio.h"

#include <vector>

namespace giga_edge {

class TtsAudioAccumulatorNode : public NodeBase {
public:
  TtsAudioAccumulatorNode(const NodeBaseConfig& base_config, const TtsAudioAccumulatorParams& params);

  void ProcessOnce() override;
  void Reset() override;

  REGISTER_BATCH_INPUT(
    tts_audio_accumulator,
    audio_chunk,
    AudioChunk,
    std::vector<float>,
    "Synthesized audio chunks (batch output of tts_streaming_audio_output)."
  );
  REGISTER_INPUT(tts_audio_accumulator, is_eos, IsEos, bool, "True when synthesis of the current phrase has finished.");

  REGISTER_OUTPUT(
    tts_audio_accumulator,
    audio,
    Audio,
    SynthesizedAudio,
    "Phrase audio: the whole phrase on eos, or a lead-in chunk followed by the rest in stream mode; "
    "no samples while there is nothing to give yet. is_last marks the end of the phrase."
  );

private:
  void Emit(bool is_last);

  TtsAudioAccumulatorParams params_;
  std::vector<float> accumulated_;
  bool streaming_started_ = false;
};

REGISTER_NODE(
  tts_audio_accumulator,
  TtsAudioAccumulatorNode,
  TtsAudioAccumulatorParams,
  "Accumulates streaming TTS chunks and emits them either as a whole phrase or as a stream with a lead-in."
);

} // namespace giga_edge
