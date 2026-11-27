#include "giga_edge/modules/spotter/nodes/extract_event_hotword/node.h"

namespace giga_edge {

ExtractEventHotwordNode::ExtractEventHotwordNode(
  const NodeBaseConfig& base_config, const ExtractEventHotwordNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void ExtractEventHotwordNode::ProcessOnce() {
  const auto& events = GetEvents();
  for (const auto& event : events) {
    for (const auto& single_event : event) {
      if (params_.event_type == single_event.event_type) {
        PushHotword(HotwordOf(single_event));
        return;
      }
    }
  }
  PushHotword("");
}

std::string ExtractEventHotwordNode::HotwordOf(const SingleSpotterEvent& event) const {
  if (!params_.asr_variable.empty()) {
    if (const auto variable = event.asr_variables.find(params_.asr_variable); variable != event.asr_variables.end()) {
      return variable->second;
    }
  }
  if (const auto form = params_.asr_forms.find(event.hot_word); form != params_.asr_forms.end()) {
    return form->second;
  }
  return params_.asr_variable.empty() ? event.hot_word : "";
}

} // namespace giga_edge
