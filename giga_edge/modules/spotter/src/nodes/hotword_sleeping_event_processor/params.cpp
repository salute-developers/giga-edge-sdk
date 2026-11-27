#include "giga_edge/modules/spotter/nodes/hotword_sleeping_event_processor/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
HotwordSleepingEventProcessorParams ParseConfig(const YAML::Node& config) {
  HotwordSleepingEventProcessorParams result;

  for (const auto& kv : config["timers"]) {
    result.timers[kv.first.as<std::string>()] = kv.second.as<int32_t>();
  }

  result.default_timer = config["default_timer"].as<int32_t>(result.default_timer);
  return result;
}

} // namespace giga_edge
