#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/tts/synthesized_audio.h"

namespace giga_edge {

class TtsPlaybackTimerNode : public NodeBase {
public:
  TtsPlaybackTimerNode(const NodeBaseConfig& base_config, const TtsPlaybackTimerParams& params);

  void ProcessOnce() override;
  void Reset() override;

  REGISTER_INPUT(tts_playback_timer, audio, Audio, SynthesizedAudio, "Phrase audio of tts_audio_accumulator.");

  REGISTER_OUTPUT(
    tts_playback_timer,
    played,
    Played,
    bool,
    "True in the tick where the playback of a phrase ends, by the count of the node; false in every other tick."
  );

  REGISTER_OPTIONAL_OUTPUT(
    tts_playback_timer,
    playing,
    Playing,
    bool,
    "True from the tick of the first chunk while the client has audio left to play after the tick, by the count of the "
    "node, and playing_tail_ms after that: phrases separated by a pause of the synthesis not longer than "
    "playing_tail_ms are one stretch."
  );

private:
  TtsPlaybackTimerParams params_;
  // The audio given to the client and not played yet.
  double left_ms_ = 0;
  // The last chunk of the phrase is given: the phrase ends when what is left is played.
  bool ending_ = false;
  // The audio the client has left to play after this tick; below zero, the time since it ran out, down to
  // -playing_tail_ms.
  double sounding_ms_;
};

REGISTER_NODE(
  tts_playback_timer,
  TtsPlaybackTimerNode,
  TtsPlaybackTimerParams,
  "Counts when the client finishes playing a phrase: the graph does not hear the speaker, so the node takes the length "
  "of the audio it has given and assumes that the client plays every chunk at once and in real time."
);

} // namespace giga_edge
