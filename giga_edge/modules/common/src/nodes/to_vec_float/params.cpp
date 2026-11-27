#include "giga_edge/modules/common/nodes/to_vec_float/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ToVecFloatNodeParams ParseConfig(const YAML::Node& config) {
  ToVecFloatNodeParams result;
  if (const auto& empty_value = config["empty_value"]) {
    result.empty_value = empty_value.as<float>();
  }
  return result;
}

} // namespace giga_edge
