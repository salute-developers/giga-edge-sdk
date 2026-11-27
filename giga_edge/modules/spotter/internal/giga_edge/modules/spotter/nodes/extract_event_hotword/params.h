#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

#include <string>
#include <unordered_map>

namespace giga_edge {

struct ExtractEventHotwordNodeParams {
  SpotterEventType event_type;
  std::string asr_variable;
  // Event hotword -> form emitted by ASR, used when asr_variable is absent.
  std::unordered_map<std::string, std::string> asr_forms;
};

} // namespace giga_edge
