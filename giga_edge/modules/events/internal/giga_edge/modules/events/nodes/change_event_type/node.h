#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ChangeEventTypeNode : public NodeBase {
public:
  ChangeEventTypeNode(const NodeBaseConfig& base_config, const ChangeEventTypeNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(change_event_type, input_events, InputEvents, std::vector<SpotterEvent>, "Input events");

  REGISTER_OPTIONAL_INPUT(
    change_event_type, flag, Flag, bool, "Optional flag to determine whether event type of this node should be changed"
  );

  REGISTER_OUTPUT(change_event_type, events, Events, std::vector<SpotterEvent>, "Changed events");

private:
  SpotterEventType input_event_type_;
  SpotterEventType output_event_type_;
};

REGISTER_NODE(change_event_type, ChangeEventTypeNode, ChangeEventTypeNodeParams, "Change event type node");

} // namespace giga_edge
