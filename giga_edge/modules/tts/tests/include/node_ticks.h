#pragma once

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"

#include <chrono>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace giga_edge::test {

inline std::vector<float> FlattenAudio(std::vector<std::vector<float>>& audio_chunks) {
  std::vector<float> flattened_audio;
  for (auto& chunk : audio_chunks) {
    flattened_audio.insert(
      flattened_audio.end(), std::make_move_iterator(chunk.begin()), std::make_move_iterator(chunk.end())
    );
  }
  return flattened_audio;
}

struct Phrase {
  std::vector<float> audio;
  bool finished = false;
};

// Ticks with an empty text until the phrase ends.
inline Phrase TickUntilEos(
  NodeContext& node, std::unordered_set<std::string> with_cancel = {}, std::optional<std::string> voice = std::nullopt
) {
  Phrase phrase;
  // A guard against a hang, not a speed measurement: under TSan on a loaded runner a long phrase takes more than 10 s.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while (!phrase.finished && std::chrono::steady_clock::now() < deadline) {
    if (!with_cancel.empty()) {
      NodePush(node, "cancel", false, true, 0);
    }
    if (voice) {
      NodePush(node, "voice", *voice, true, 0);
    }
    NodePush(node, "text", std::string());
    auto chunks = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
    auto audio = FlattenAudio(chunks);
    phrase.audio.insert(phrase.audio.end(), audio.begin(), audio.end());
    phrase.finished = NodeGet<bool>(node, "is_eos");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return phrase;
}

} // namespace giga_edge::test
