#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class FilterHotwordEventsNode : public NodeBase {
public:
  FilterHotwordEventsNode(const NodeBaseConfig& base_config, const FilterHotwordEventsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(filter_hotword_events, events, Events, std::vector<SpotterEvent>, "Spotter events");
  REGISTER_OUTPUT(
    filter_hotword_events,
    filtered_events,
    FilteredEvents,
    std::vector<SpotterEvent>,
    "Spotter events which contain single events with required hotwords"
  );

private:
  const FilterHotwordEventsNodeParams params_;
};

REGISTER_NODE(
  filter_hotword_events,
  FilterHotwordEventsNode,
  FilterHotwordEventsNodeParams,
  "Filters spotter events to spotter events which contain single events with required hotwords"
);

} // namespace giga_edge
