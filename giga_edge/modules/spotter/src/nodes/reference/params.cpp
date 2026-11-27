#include "giga_edge/modules/spotter/nodes/reference/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ReferenceEventsNodeParams ParseConfig(const YAML::Node& config) {
  ReferenceEventsNodeParams result;

  for (const auto& timer : config["reference_timer"]) {
    result.reference_timer.push_back(timer.as<int>());
  }
  return result;
}

} // namespace giga_edge