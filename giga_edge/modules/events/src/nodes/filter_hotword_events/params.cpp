#include "giga_edge/modules/events/nodes/filter_hotword_events/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
FilterHotwordEventsNodeParams ParseConfig(const YAML::Node& config) {
  FilterHotwordEventsNodeParams result;
  for (const auto& hotword : config["hotwords"]) {
    result.hotwords.emplace(hotword.as<std::string>());
  }
  return result;
}

} // namespace giga_edge