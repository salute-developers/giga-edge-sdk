#include "giga_edge/modules/audio/nodes/audio_iterator/node.h"

namespace giga_edge {

AudioIteratorNode::AudioIteratorNode(const NodeBaseConfig& base_config, const ChunkIteratorParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void AudioIteratorNode::ProcessOnce() {
  const auto& audio = GetAudio();
  if (audio.empty()) {
    PushAudioChunk({});
    return;
  }

  if (channel_iterators_.empty()) {
    channel_iterators_.reserve(audio.size());
    for (int i = 0; i < audio.size(); ++i) {
      channel_iterators_.emplace_back(params_);
    }
  }

  if (audio.size() != channel_iterators_.size()) {
    throw std::runtime_error(
      "AudioIteratorNode: Number of channels changed from " + std::to_string(channel_iterators_.size()) + " to "
      + std::to_string(audio.size())
    );
  }

  for (int i = 0; i < audio.size(); ++i) {
    channel_iterators_[i].Push(audio[i]);
  }

  do {
    std::vector<std::vector<float>> chunk;
    chunk.reserve(channel_iterators_.size());
    for (auto& channel_iterator : channel_iterators_) {
      chunk.push_back(channel_iterator.Next());
    }
    PushAudioChunk(std::move(chunk));
  } while (channel_iterators_[0].HasNext());
}

void AudioIteratorNode::Reset() {
  for (auto& channel_iterator : channel_iterators_) {
    channel_iterator.Reset();
  }
}

} // namespace giga_edge
