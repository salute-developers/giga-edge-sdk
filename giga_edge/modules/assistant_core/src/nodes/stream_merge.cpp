#include "giga_edge/modules/assistant_core/nodes/stream_merge/node.h"

namespace giga_edge {
namespace {

bool IsIdle(const AssistantChunk& chunk) {
  return !chunk.meta.is_final && chunk.meta.status == AssistantStatus::OK
    && std::holds_alternative<std::monostate>(chunk.payload);
}

} // namespace

StreamMerge::StreamMerge(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void StreamMerge::ProcessOnce() {
  size_t pushed = 0;
  RunnerPhase phase = RunnerPhase::IDLE;
  const auto first = GetFirstBatch();
  const auto second = GetSecondBatch();
  for (const auto* batch : {&first, &second}) {
    for (const auto* chunk : *batch) {
      if (chunk->meta.phase != RunnerPhase::IDLE) {
        phase = chunk->meta.phase;
      }
      if (IsIdle(*chunk)) {
        continue;
      }
      PushStreamChunk(*chunk);
      ++pushed;
    }
  }
  if (pushed == 0) {
    PushStreamChunk(MakeIdleChunk(phase));
  }
}

} // namespace giga_edge
