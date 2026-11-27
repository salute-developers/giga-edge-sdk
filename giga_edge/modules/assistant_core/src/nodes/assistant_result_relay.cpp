#include "giga_edge/modules/assistant_core/nodes/assistant_result_relay/node.h"

namespace giga_edge {

AssistantResultRelay::AssistantResultRelay(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void AssistantResultRelay::ProcessOnce() {
  auto result = GetInput();
  if (const auto trace = GetTrace(); trace.has_value() && !trace->get().request_id.empty()) {
    result.trace = trace->get();
  }
  PushAssistantResult(std::move(result));
}

} // namespace giga_edge
