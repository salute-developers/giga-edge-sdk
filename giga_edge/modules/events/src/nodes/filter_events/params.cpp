#include "giga_edge/modules/events/nodes/filter_events/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
FilterEventsNodeParams ParseConfig(const YAML::Node& config) {
  FilterEventsNodeParams result;
  for (const auto& event_type : config["event_types"]) {
    result.event_types.emplace(EnumFromString<SpotterEventType>(event_type.as<std::string>()));
  }
  return result;
}

} // namespace giga_edge