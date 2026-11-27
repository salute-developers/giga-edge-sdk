#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class FilterEventsNode : public NodeBase {
public:
  FilterEventsNode(const NodeBaseConfig& base_config, const FilterEventsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(filter_events, events, Events, std::vector<SpotterEvent>, "Spotter events");
  REGISTER_OUTPUT(filter_events, event, Event, SpotterEvent, "Spotter event with all filtered single events");

private:
  const FilterEventsNodeParams params_;
};

REGISTER_NODE(
  filter_events,
  FilterEventsNode,
  FilterEventsNodeParams,
  "Filters spotter events to one event with single events of required types"
);

} // namespace giga_edge
