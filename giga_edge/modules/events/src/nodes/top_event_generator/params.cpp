#include "giga_edge/modules/events/nodes/top_event_generator/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
TopEventGeneratorNodeParams ParseConfig(const YAML::Node& config) {
  TopEventGeneratorNodeParams result;

  result.num_classes = config["num_classes"].as<int>();
  return result;
}

} // namespace giga_edge