#include "giga_edge/modules/common/nodes/compare/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
CompareOpNodeParams ParseConfig(const YAML::Node& config) {
  CompareOpNodeParams result;
  result.compare_op_type = EnumFromString<CompareOpType>(config["compare_op_type"].as<std::string>());
  return result;
}

} // namespace giga_edge