#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

namespace giga_edge {

class RequestGate : public NodeBase {
public:
  explicit RequestGate(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(request_gate, assistant_request, AssistantRequest, AssistantRequest, "Request to pass or hold back.");
  REGISTER_INPUT(request_gate, open, Open, bool, "True passes the request, false turns it into an idle tick.");
  REGISTER_OUTPUT(request_gate, assistant_request, GatedRequest, AssistantRequest, "Request or an idle tick.");
};

REGISTER_NODE(request_gate, RequestGate, "Routes an AssistantRequest to a runner only while the gate is open.");

} // namespace giga_edge
