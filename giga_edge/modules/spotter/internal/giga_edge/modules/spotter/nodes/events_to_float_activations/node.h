#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class EventsToFloatActivationsNode : public NodeBase {
public:
  explicit EventsToFloatActivationsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(events_to_float_activations, events, Events, std::vector<SpotterEvent>, "Spotter events");
  REGISTER_OUTPUT(
    events_to_float_activations,
    floats,
    Floats,
    std::vector<float>,
    "0/1 if there is an activation for given class in any event"
  );
};

REGISTER_NODE(
  events_to_float_activations, EventsToFloatActivationsNode, "Transforms events to scores-like vector of floats"
);

} // namespace giga_edge
