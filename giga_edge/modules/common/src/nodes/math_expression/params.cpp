#include "giga_edge/modules/common/nodes/math_expression/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
MathExpressionNodeParams ParseConfig(const YAML::Node& config) {
  MathExpressionNodeParams result;
  result.expression = config["expression"].as<std::string>();
  return result;
}

} // namespace giga_edge
