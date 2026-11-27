#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class ExtractEventHotwordNode : public NodeBase {
public:
  ExtractEventHotwordNode(const NodeBaseConfig& base_config, const ExtractEventHotwordNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(extract_event_hotword, events, Events, std::vector<SpotterEvent>, "Spotter events");
  REGISTER_OUTPUT(
    extract_event_hotword,
    hotword,
    Hotword,
    std::string,
    "First found hotword of given spotter event type: its ASR variable named by `asr_variable`, else its form from "
    "`asr_forms`"
  );

private:
  std::string HotwordOf(const SingleSpotterEvent& event) const;

  ExtractEventHotwordNodeParams params_;
};

REGISTER_NODE(
  extract_event_hotword,
  ExtractEventHotwordNode,
  ExtractEventHotwordNodeParams,
  "Extracts first hortword of given spotter event type"
);

} // namespace giga_edge
