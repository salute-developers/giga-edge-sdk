#pragma once

#include <vector>

namespace giga_edge {

// Audio of the assistant reply: a chunk of a phrase and the end-of-phrase mark.
struct SynthesizedAudio {
  std::vector<float> samples;
  int sample_rate = 0;
  bool is_last = false;
};

} // namespace giga_edge
