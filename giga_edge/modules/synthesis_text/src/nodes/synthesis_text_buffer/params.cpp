#include "giga_edge/modules/synthesis_text/nodes/synthesis_text_buffer/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
SynthesisTextBufferParams ParseConfig(const YAML::Node& config) {
  SynthesisTextBufferParams result;
  if (!config.IsDefined()) {
    return ParseConfig<SynthesisTextBufferParams>(YAML::Node());
  }
  result.reset_synthesis_text_delay = config["reset_synthesis_text_delay"].as<size_t>(0);
  return result;
}

} // namespace giga_edge