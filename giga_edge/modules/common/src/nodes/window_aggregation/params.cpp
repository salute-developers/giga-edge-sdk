#include "giga_edge/modules/common/nodes/window_aggregation/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/enum/enum_fwd.h"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace giga_edge {

DefaultValueStrategy ParseDefaultValueStrategy(const std::string& default_value_strategy) {
  static std::unordered_map<std::string, DefaultValueStrategy> const table = {
    {"fill", DefaultValueStrategy::FILL},
    {"const", DefaultValueStrategy::CONST},
  };

  auto it = table.find(default_value_strategy);
  if (it != table.end()) {
    return it->second;
  } else {
    throw std::runtime_error("Unknown aggregation type " + default_value_strategy);
  }
}

template<>
DefaultValueParams ParseConfig(const YAML::Node& config) {
  if (!config.IsDefined()) {
    return ParseConfig<DefaultValueParams>(YAML::Node());
  }

  DefaultValueParams result;
  result.strategy = ParseDefaultValueStrategy(config["strategy"].as<std::string>("fill"));
  result.value = config["value"].as<float>(0.);
  return result;
}

template<>
SlidingWindowParams ParseConfig(const YAML::Node& config) {
  if (!config.IsDefined()) {
    return ParseConfig<SlidingWindowParams>(YAML::Node());
  }

  SlidingWindowParams result;
  result.window_size = config["window_size"].as<int32_t>();
  if (result.window_size < 1) {
    throw std::invalid_argument("window_aggregation: window_size must be positive");
  }
  result.aggregation_type = EnumFromString<AggregationType>(config["aggregation_type"].as<std::string>("avg"));
  result.default_value = ParseConfig<DefaultValueParams>(config["default_value"]);
  return result;
}

template<>
WindowAggregationNodeParams ParseConfig(const YAML::Node& config) {
  WindowAggregationNodeParams result;

  for (const auto& aggregation : config["aggregations"]) {
    result.aggregations.push_back(ParseConfig<SlidingWindowParams>(aggregation));
  }
  result.output_interval = config["output_interval"].as<int32_t>(1);
  if (result.output_interval < 1) {
    throw std::invalid_argument("window_aggregation: output_interval must be positive");
  }
  return result;
}

} // namespace giga_edge
