#pragma once

namespace giga_edge {

struct TtsAudioAccumulatorParams {
  bool stream = false;

  // Audio lead before the first chunk: without it the player catches up with the synthesis (RTF > 1) and the phrase
  // breaks up.
  int min_lead_ms = 0;

  int sample_rate = 16000;
};

} // namespace giga_edge
