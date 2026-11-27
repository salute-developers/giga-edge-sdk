#pragma once

#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/modules/web/environment.h"
#include "giga_edge/modules/web/web_coordinator.h"

#include <memory>
#include <string>

namespace giga_edge {
namespace web {
namespace nodes {

inline std::shared_ptr<IWebCoordinator> ResolveCoordinator(
  const NodeBaseConfig& config, const std::string& environment_name, std::string& error
) {
  if (config.environments == nullptr) {
    error = "Web node requires model environments.";
    return {};
  }
  const auto env_it = config.environments->find(environment_name);
  if (env_it == config.environments->end() || !env_it->second) {
    error = "Web environment is not available: " + environment_name;
    return {};
  }
  auto web_env = std::dynamic_pointer_cast<WebServiceEnvironment>(env_it->second);
  if (!web_env) {
    error = "Environment is not WebServiceEnvironment: " + environment_name;
    return {};
  }
  if (!web_env->coordinator) {
    error = "Web coordinator is not initialized: " + environment_name;
    return {};
  }
  return web_env->coordinator;
}

} // namespace nodes
} // namespace web
} // namespace giga_edge
