#include "giga_edge/modules/audio/nodes/resampler/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

namespace giga_edge {
template<>
ResamplerParams ParseConfig(const YAML::Node& config) {
  ResamplerParams result;

  result.from_freq = config["from_freq"].as<uint32_t>();
  result.to_freq = config["to_freq"].as<uint32_t>();
  return result;
}
} // namespace giga_edge