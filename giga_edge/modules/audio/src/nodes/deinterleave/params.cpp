#include "giga_edge/modules/audio/nodes/deinterleave/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
DeinterleaveNodeParams ParseConfig(const YAML::Node& config) {
  DeinterleaveNodeParams result;
  PARSE_REQUIRED_FIELD(num_channels);
  return result;
}

} // namespace giga_edge