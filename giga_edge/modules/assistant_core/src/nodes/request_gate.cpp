#include "giga_edge/modules/assistant_core/nodes/request_gate/node.h"

namespace giga_edge {

RequestGate::RequestGate(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void RequestGate::ProcessOnce() {
  AssistantRequest request = GetAssistantRequest();
  if (!GetOpen() && request.kind != AssistantRequestKind::NONE) {
    AssistantRequest idle;
    idle.request_id = request.request_id;
    PushGatedRequest(std::move(idle));
    return;
  }
  PushGatedRequest(std::move(request));
}

} // namespace giga_edge
