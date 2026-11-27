#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

namespace giga_edge {

class AssistantResultRelay : public NodeBase {
public:
  explicit AssistantResultRelay(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(assistant_result_relay, input, Input, AssistantResult, "Result under an internal name.");
  REGISTER_OPTIONAL_INPUT(
    assistant_result_relay, trace, Trace, TurnTrace, "Turn trace attached to the result when there is one."
  );
  REGISTER_OUTPUT(
    assistant_result_relay,
    assistant_result,
    AssistantResult,
    AssistantResult,
    "The result, with the turn trace when one is given."
  );
};

REGISTER_NODE(
  assistant_result_relay,
  AssistantResultRelay,
  "Publishes a result into the submodel output: nodes of a version must not read the output of their own submodel."
);

} // namespace giga_edge
