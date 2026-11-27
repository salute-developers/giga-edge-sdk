#include "giga_edge/modules/common/nodes/aggregation/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
AggregationNodeParams ParseConfig(const YAML::Node& config) {
  AggregationNodeParams result;
  result.aggregation = EnumFromString<AggregationType>(config["aggregation"].as<std::string>());
  return result;
}

} // namespace giga_edge