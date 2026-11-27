#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/params.h"
#include "giga_edge/modules/spotter/sleeping_event_processor.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class SleepingSpotterEventProcessorNode : public NodeBase {
public:
  SleepingSpotterEventProcessorNode(const NodeBaseConfig& base_config, const SleepTimerParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(sleeping_event_processor, events, Events, std::vector<SpotterEvent>, "Events");
  REGISTER_OUTPUT(
    sleeping_event_processor, events, Events, std::vector<SpotterEvent>, "Events with converted subactivations"
  );

private:
  SleepingSpotterEventProcessor event_processor_;
};

REGISTER_NODE(
  sleeping_event_processor,
  SleepingSpotterEventProcessorNode,
  SleepTimerParams,
  "Converts activations to subactivations if had recent actications"
);

} // namespace giga_edge
