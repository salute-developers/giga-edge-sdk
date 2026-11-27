#pragma once

namespace giga_edge {

struct TtsPlaybackTimerParams {
  // The time of one tick of the graph, ms: the length of the microphone chunk it is fed with.
  int tick_ms = 0;

  // Added to the length of a phrase: the delay of the sound device, which the graph does not see.
  int tail_ms = 0;

  // How long playing stays true after the end of the audio: the delay of the sound device and of the microphone, the
  // echo of the room, the lag of whatever listens to the microphone.
  int playing_tail_ms = 0;
};

} // namespace giga_edge
