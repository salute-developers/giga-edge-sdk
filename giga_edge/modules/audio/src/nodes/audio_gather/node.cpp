#include "giga_edge/modules/audio/nodes/audio_gather/node.h"

#include <numeric>

namespace giga_edge {

AudioGatherNode::AudioGatherNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void AudioGatherNode::ProcessOnce() {
  const auto& audio_chunks = GetAudioBatch();

  if (audio_chunks.empty()) {
    throw std::runtime_error("AudioGatherNode: empty input");
  }

  std::vector<std::vector<float>> result = *audio_chunks[0];
  for (const auto& chunk : audio_chunks) {
    if (chunk->size() != result.size()) {
      throw std::runtime_error(Id() + ": Number of channels it items inside batch differ");
    }
  }

  if (result.empty()) {
    PushAudio(result);
    return;
  }

  auto total_size = std::accumulate(
    audio_chunks.begin(), audio_chunks.end(), 0, [](const auto& size, const auto& chunk) {
      return size + chunk[0].size();
    }
  );

  for (auto& channel : result) {
    channel.reserve(total_size);
  }

  for (int i = 1; i < audio_chunks.size(); ++i) {
    for (int j = 0; j < audio_chunks[i]->size(); ++j) {
      result[j].insert(result[j].end(), (*audio_chunks[i])[j].begin(), (*audio_chunks[i])[j].end());
    }
  }

  PushAudio(result);
}

} // namespace giga_edge
