#include "giga_edge/modules/enhance/nodes/denoiser/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

#include "yaml-cpp/yaml.h"

#include <sstream>

namespace giga_edge {

template<>
DenoiserParams ParseConfig(const YAML::Node& config) {
  DenoiserParams p;

  p.sampling_freq = config["sampling_freq"].as<uint32_t>();
  p.frame_stride_ms = config["frame_stride_ms"].as<uint32_t>();

  YAML::Emitter emitter;
  emitter << config["config"];

  p.config_yaml = emitter.c_str();

  return p;
}
} // namespace giga_edge