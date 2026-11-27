#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class EventsStubNode : public NodeBase {
public:
  explicit EventsStubNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(events_stub, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(events_stub, events, Events, std::vector<SpotterEvent>, "Empty events vector");
};

REGISTER_NODE(events_stub, EventsStubNode, "Stub, that generates empty events");

} // namespace giga_edge
