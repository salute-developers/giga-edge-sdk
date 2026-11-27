#include "giga_edge/modules/common/nodes/transpose/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
TransposeNodeParams ParseConfig(const YAML::Node& config) {
  TransposeNodeParams result;
  result.columns = config["columns"].as<int>();
  return result;
}

} // namespace giga_edge
