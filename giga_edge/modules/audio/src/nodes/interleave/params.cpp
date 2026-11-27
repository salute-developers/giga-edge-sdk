#include "giga_edge/modules/audio/nodes/interleave/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
InterleaveNodeParams ParseConfig(const YAML::Node& config) {
  InterleaveNodeParams result;
  if (config.IsDefined()) {
    for (const auto& channel : config["channels"]) {
      result.channels.push_back(channel.as<uint32_t>());
    }
  }
  return result;
}

} // namespace giga_edge