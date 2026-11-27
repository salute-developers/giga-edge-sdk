#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/nodes/hotword_sleeping_event_processor/params.h"
#include "giga_edge/modules/spotter/spotter_event.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class HotwordSleepingEventProcessorNode : public NodeBase {
public:
  HotwordSleepingEventProcessorNode(
    const NodeBaseConfig& base_config, const HotwordSleepingEventProcessorParams& params
  );

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(hotword_sleeping_event_processor, events, Events, std::vector<SpotterEvent>, "Events");
  REGISTER_OUTPUT(
    hotword_sleeping_event_processor, events, Events, std::vector<SpotterEvent>, "Events with converted subactivations"
  );

private:
  int32_t LookupTimer(const std::string& hotword) const;

  HotwordSleepingEventProcessorParams params_;
  std::unordered_map<std::string, int32_t> sleeping_timer_current_value_;
};

REGISTER_NODE(
  hotword_sleeping_event_processor,
  HotwordSleepingEventProcessorNode,
  HotwordSleepingEventProcessorParams,
  "Converts activations to subactivations using independent per-hotword timers; timer ticks once per node call"
);

} // namespace giga_edge
