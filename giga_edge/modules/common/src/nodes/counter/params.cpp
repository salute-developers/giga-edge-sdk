#include "giga_edge/modules/common/nodes/counter/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
CounterNodeParams ParseConfig(const YAML::Node& config) {
  CounterNodeParams result;
  result.init_value = config["init_value"].as<float>(0.0f);
  return result;
}

} // namespace giga_edge