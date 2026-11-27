#include "giga_edge/modules/common/nodes/thresholds_compare/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
ThresholdsCompareNodeParams ParseConfig(const YAML::Node& config) {
  ThresholdsCompareNodeParams result;

  for (const auto& threshold : config["thresholds"]) {
    result.thresholds.push_back(threshold.as<float>());
  }
  return result;
}

} // namespace giga_edge