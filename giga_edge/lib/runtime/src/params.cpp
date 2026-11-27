#include "giga_edge/lib/runtime/params.h"

#include <giga_edge/lib/config_parser/config_parser.h>
#include <giga_edge/lib/enum/enum.h>

#include <stdexcept>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::SubmodelRequireMode, "none", "immediate", "selective", "recursive")

namespace giga_edge {

template<>
SubmodelSignature ParseConfig(const YAML::Node& config) {
  SubmodelSignature result;
  result.id = config["id"].as<std::string>();

  for (const auto& input : config["inputs"]) {
    result.inputs.emplace(input.first.as<std::string>(), input.second.as<std::string>());
  }

  for (const auto& output : config["outputs"]) {
    result.outputs.emplace(output.first.as<std::string>(), output.second.as<std::string>());
  }
  return result;
}

template<>
SubmodelFeature ParseConfig(const YAML::Node& config) {
  SubmodelFeature result;

  for (const auto& feature_id : config["feature_ids"]) {
    result.feature_ids.insert(feature_id.as<std::string>());
  }

  result.submodel_id = config["submodel_id"].as<std::string>();
  result.submodel_version = config["submodel_version"].as<std::string>();

  result.require_mode = EnumFromString<SubmodelRequireMode>(config["require_mode"].as<std::string>("none"));
  return result;
}

template<>
DefaultSubmodel ParseConfig(const YAML::Node& config) {
  if (config.IsScalar()) {
    return DefaultSubmodel{.submodel_version = config.as<std::string>()};
  }

  DefaultSubmodel result;

  result.submodel_version = config["submodel_version"].as<std::string>();
  result.require_mode = EnumFromString<SubmodelRequireMode>(config["require_mode"].as<std::string>("none"));

  return result;
}

template<>
SubmodelParams ParseConfig(const YAML::Node& config) {
  SubmodelParams result;

  result.version = config["version"].as<std::string>("");
  result.lib_version = config["lib_version"].as<std::string>("");

  for (const auto& node : config["nodes"]) {
    result.nodes.push_back(ParseConfig<NodeParams>(node));
  }

  for (const auto& node : config["submodels"]) {
    result.submodels.push_back(ParseConfig<SubmodelSignature>(node));
  }

  for (const auto& default_submodel : config["default_submodels"]) {
    result.default_submodels.emplace(
      default_submodel.first.as<std::string>(), ParseConfig<DefaultSubmodel>(default_submodel.second)
    );
  }

  for (const auto& features : config["features"]) {
    result.features.push_back(ParseConfig<SubmodelFeature>(features));
  }

  if (config["model_environments"]) {
    result.environments_map_params = ParseConfig<EnvironmentsMapParams>(config);
  }

  return result;
}

template<>
RuntimeParams ParseConfig(const YAML::Node& config) {
  RuntimeParams result{ParseConfig<SubmodelParams>(config)};
  result.num_threads = config["num_threads"].as<int>();
  result.load_threads = config["load_threads"].as<int>(1);
  if (result.load_threads < 1) {
    throw std::invalid_argument("load_threads must be at least 1");
  }
  result.realtime_priority = config["realtime_priority"].as<bool>(false);
  result.num_realtime_threads = config["num_realtime_threads"].as<int>(0);
  result.thread_name_prefix = config["thread_name_prefix"].as<std::string>("");

  result.common_node_params = ParseConfig<CommonNodeParams>(config["common_node_params"]);
  result.statistics_every_batches = config["statistics_every_batches"].as<int>(std::numeric_limits<int>::max());

  return result;
}

} // namespace giga_edge
