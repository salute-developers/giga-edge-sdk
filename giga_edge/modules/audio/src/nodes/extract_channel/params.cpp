#include "giga_edge/modules/audio/nodes/extract_channel/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ExtractChannelNodeParams ParseConfig(const YAML::Node& config) {
  ExtractChannelNodeParams result;
  PARSE_REQUIRED_FIELD(channel);
  return result;
}

} // namespace giga_edge