#include "giga_edge/modules/common/nodes/to_bool/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ToBoolNodeParams ParseConfig(const YAML::Node& config) {
  ToBoolNodeParams result;
  result.threshold = config["threshold"].as<float>(result.threshold);
  return result;
}

} // namespace giga_edge