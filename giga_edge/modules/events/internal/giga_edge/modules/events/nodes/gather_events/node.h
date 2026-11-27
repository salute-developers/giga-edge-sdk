#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class GatherEventsNode : public NodeBase {
public:
  GatherEventsNode(const NodeBaseConfig& base_config, const GatherEventsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_DYNAMIC_INPUTS(gather_events);
  REGISTER_OUTPUT(gather_events, event, Event, SpotterEvent, "Gathered spotter event")

private:
  std::unordered_set<std::string> input_names_;
  GatherEventsNodeParams params_;
};

REGISTER_NODE(
  gather_events,
  GatherEventsNode,
  GatherEventsNodeParams,
  "Takes all single events with required types from inputs and concats them into one result event"
);

} // namespace giga_edge
