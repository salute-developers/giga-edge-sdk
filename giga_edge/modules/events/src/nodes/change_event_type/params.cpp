#include "giga_edge/modules/events/nodes/change_event_type/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
ChangeEventTypeNodeParams ParseConfig(const YAML::Node& config) {
  ChangeEventTypeNodeParams result;

  result.input_event_type = EnumFromString<SpotterEventType>(config["input_event_type"].as<std::string>());
  result.output_event_type = EnumFromString<SpotterEventType>(config["output_event_type"].as<std::string>());

  return result;
}

} // namespace giga_edge