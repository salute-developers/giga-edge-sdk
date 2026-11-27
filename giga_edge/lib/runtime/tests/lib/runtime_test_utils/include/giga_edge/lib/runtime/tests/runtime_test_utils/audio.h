#pragma once

#include "giga_edge/lib/runtime/runtime.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge::tests {

using AudioData = std::unordered_map<std::string, std::vector<float>>;
using ChunkedAudioData = std::unordered_map<std::string, std::vector<std::vector<float>>>;

enum class InputOrder {
  ALTERNATING, // input1 -> input2 -> input1 -> input2
  RANDOM,
  ALTERNATING_ALL, // input1 -> input1 -> ... -> input1 -> input2 -> input2 -> ... -> input2
  WITH_RESET,      // all input1 -> reset -> all input2
};

ChunkedAudioData ChunkWavs(const AudioData& wav_data, int chunk_size);

void ApplyAudio(Runtime* runtime, const ChunkedAudioData& audio_data, InputOrder input_order = InputOrder::ALTERNATING);

} // namespace giga_edge::tests