#include "giga_edge/modules/enhance/nodes/echo_cancelation/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

#include "yaml-cpp/yaml.h"

#include <sstream>

namespace giga_edge {

template<>
EchoCancelationParams ParseConfig(const YAML::Node& config) {
  EchoCancelationParams result;

  YAML::Emitter emitter;
  emitter << config["config"];

  result.config_yaml = emitter.c_str();
  result.frame_len = config["frame_len"].as<uint32_t>();

  return result;
}
} // namespace giga_edge