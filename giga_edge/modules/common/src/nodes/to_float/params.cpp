#include "giga_edge/modules/common/nodes/to_float/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ToFloatNodeParams ParseConfig(const YAML::Node& config) {
  ToFloatNodeParams result;
  result.on_empty = config["on_empty"].as<float>(result.on_empty);
  return result;
}

} // namespace giga_edge