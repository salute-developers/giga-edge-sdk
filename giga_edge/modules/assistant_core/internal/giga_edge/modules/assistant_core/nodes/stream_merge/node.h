#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

namespace giga_edge {

class StreamMerge : public NodeBase {
public:
  explicit StreamMerge(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_BATCH_INPUT(stream_merge, first, First, AssistantChunk, "Chunk stream of the first runner.");
  REGISTER_BATCH_INPUT(stream_merge, second, Second, AssistantChunk, "Chunk stream of the second runner.");
  REGISTER_BATCH_OUTPUT(
    stream_merge, stream_chunk, StreamChunk, AssistantChunk, "Merged stream: idle ticks collapsed into one."
  );
};

REGISTER_NODE(stream_merge, StreamMerge, "Merges the chunk streams of two runners into one.");

} // namespace giga_edge
