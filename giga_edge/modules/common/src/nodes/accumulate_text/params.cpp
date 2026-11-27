#include "giga_edge/modules/common/nodes/accumulate_text/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
AccumulateTextNodeParams ParseConfig(const YAML::Node& config) {
  AccumulateTextNodeParams result;
  result.clear_after_push = config["clear_after_push"].as<bool>(result.clear_after_push);
  return result;
}

} // namespace giga_edge
