#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/events.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class HasActivationNode : public NodeBase {
public:
  explicit HasActivationNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(
    has_activation,
    events,
    Events,
    std::vector<SpotterEvent>,
    "Spotter events: return true if any event is ACTIVATION, false otherwise"
  );

  REGISTER_OUTPUT(has_activation, output, Output, bool, "True if any event is ACTIVATION, false otherwise");

private:
};

REGISTER_NODE(has_activation, HasActivationNode, "Checks if any spotter event is ACTIVATION type");

} // namespace giga_edge
