#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <vector>

namespace giga_edge {

class AssistantResultStub : public NodeBase {
public:
  explicit AssistantResultStub(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override;

private:
  REGISTER_INPUT(assistant_result_stub, input, Input, std::vector<float>, "Tick source.");
  REGISTER_OPTIONAL_INPUT(
    assistant_result_stub, manual_activation, ManualActivation, bool, "Manual activation: answered by an empty session."
  );
  REGISTER_OUTPUT(assistant_result_stub, assistant_result, AssistantResult, AssistantResult, "Empty assistant result.");
  REGISTER_OPTIONAL_OUTPUT(
    assistant_result_stub,
    events,
    Events,
    AssistantSessionEvents,
    "Session events: an empty session for every manual activation; after a reset an empty reply closes the session "
    "left by the assistant."
  );

  bool close_dropped_ = false;
};

REGISTER_NODE(assistant_result_stub, AssistantResultStub, "Stub that emits an empty assistant result on every tick.");

} // namespace giga_edge
