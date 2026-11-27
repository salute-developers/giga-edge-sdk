#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/environment/environment.h"

#include <stdexcept>

namespace giga_edge {

template<>
ThreadPoolEnvironmentParams ParseConfig(const YAML::Node& config) {
  ThreadPoolEnvironmentParams result;
  if (config) {
    result.threads_count = config["threads_count"].as<int>(1);
    result.realtime_priority = config["realtime_priority"].as<bool>(false);
    if (result.realtime_priority) {
      result.realtime_threads_count = config["realtime_threads_count"].as<int>(result.threads_count);
    }
    result.thread_name_prefix = config["thread_name_prefix"].as<std::string>("");
  }
  return result;
}

template<>
EnvironmentsMapParams ParseConfig(const YAML::Node& config) {
  EnvironmentsMapParams result;
  for (const auto& env_node : config["model_environments"]) {
    const auto name = env_node.first.as<std::string>();
    const auto& params_node = env_node.second;
    if (!params_node["type"]) {
      throw std::runtime_error("Environment '" + name + "' must have a 'type' field.");
    }
    const auto type_str = params_node["type"].as<std::string>();

    EnvironmentParams env_params;
    if (type_str == "thread_pool") {
      env_params.type = EnvironmentParams::Type::THREAD_POOL;
      env_params.thread_pool_params = ParseConfig<ThreadPoolEnvironmentParams>(params_node["thread_pool"]);
    } else if (type_str == "empty") {
      env_params.type = EnvironmentParams::Type::EMPTY;
    } else {
      const auto* handlers = FindExternalEnvironment(type_str);
      if (handlers == nullptr || !handlers->parse) {
        throw std::runtime_error("Environment '" + name + "': type '" + type_str + "' is not supported in this build.");
      }
      env_params.type = EnvironmentParams::Type::EXTERNAL;
      env_params.external_type = type_str;
      env_params.external_config = handlers->parse(params_node[type_str]);
    }

    result[name] = env_params;
  }
  return result;
}

} // namespace giga_edge
