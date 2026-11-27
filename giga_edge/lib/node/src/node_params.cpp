#include "giga_edge/lib/node/node_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
CommonNodeParams ParseConfig(const YAML::Node& config) {
  CommonNodeParams result;
  if (!config.IsDefined()) {
    return ParseConfig<CommonNodeParams>(YAML::Node());
  }
  result.statistics_length = config["statistics_length"].as<int>(0);
  return result;
}

template<>
NodeParams ParseConfig(const YAML::Node& config) {
  NodeParams result;
  result.type = config["type"].as<std::string>();
  result.name = config["name"].as<std::string>();

  for (const auto& input : config["inputs"]) {
    result.inputs.emplace(input.first.as<std::string>(), input.second.as<std::string>());
  }

  for (const auto& output : config["outputs"]) {
    result.outputs.emplace(output.first.as<std::string>(), output.second.as<std::string>());
  }

  result.priority = config["priority"].as<int>(0);
  if (config["params"].IsDefined()) {
    result.params = config["params"];
  }

  return result;
}

} // namespace giga_edge