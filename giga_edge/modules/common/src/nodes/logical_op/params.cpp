#include "giga_edge/modules/common/nodes/logical_op/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

namespace giga_edge {

template<>
LogicalOpNodeParams ParseConfig(const YAML::Node& config) {
  LogicalOpNodeParams result;
  result.logical_op_type = EnumFromString<LogicalOpType>(config["logical_op_type"].as<std::string>());
  return result;
}

} // namespace giga_edge