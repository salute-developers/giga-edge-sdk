#pragma once

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class AudioIteratorNode : public NodeBase {
public:
  AudioIteratorNode(const NodeBaseConfig& base_config, const ChunkIteratorParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(audio_iterator, audio, Audio, std::vector<std::vector<float>>, "Channeled audio");

  REGISTER_BATCH_OUTPUT(
    audio_iterator, audio_chunks, AudioChunk, std::vector<std::vector<float>>, "Channeled audio chunks"
  );

private:
  ChunkIteratorParams params_;
  std::vector<ChunkIterator<float>> channel_iterators_;
};

REGISTER_NODE(audio_iterator, AudioIteratorNode, ChunkIteratorParams, "Iterator for channeled audio");

} // namespace giga_edge
