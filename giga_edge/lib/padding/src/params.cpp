#include "giga_edge/lib/padding/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::PaddingAppendPlace, "begin", "end");
GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::PaddingStrategy, "always_add", "pad_to_size");

namespace giga_edge {

template<>
PaddingParams ParseConfig(const YAML::Node& config) {
  PaddingParams result;

  result.count = config["count"].as<int>(0);
  result.value = config["value"].as<float>(0.0);

  if (config["append_place"]) {
    result.append_place = EnumFromString<PaddingAppendPlace>(config["append_place"].as<std::string>());
  }
  if (config["padding_strategy"]) {
    result.padding_strategy = EnumFromString<PaddingStrategy>(config["padding_strategy"].as<std::string>());
  }

  return result;
}

} // namespace giga_edge
