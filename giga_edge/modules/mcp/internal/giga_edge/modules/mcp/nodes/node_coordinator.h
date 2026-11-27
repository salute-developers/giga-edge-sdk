#pragma once

#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/environment.h"

#include <memory>
#include <string>

namespace giga_edge {
namespace mcp {
namespace nodes {

inline std::shared_ptr<IMcpCoordinator> ResolveCoordinator(
  const NodeBaseConfig& config, const std::string& environment_name, std::string& error
) {
  if (config.environments == nullptr) {
    error = "MCP node requires model environments.";
    return {};
  }

  const auto env_it = config.environments->find(environment_name);
  if (env_it == config.environments->end() || !env_it->second) {
    error = "MCP environment is not available: " + environment_name;
    return {};
  }

  auto mcp_env = std::dynamic_pointer_cast<McpServiceEnvironment>(env_it->second);
  if (!mcp_env) {
    error = "Environment is not McpServiceEnvironment: " + environment_name;
    return {};
  }
  if (!mcp_env->coordinator) {
    error = "MCP coordinator is not initialized: " + environment_name;
    return {};
  }
  return mcp_env->coordinator;
}

inline McpConsumerId EnsureConsumer(
  const std::shared_ptr<IMcpCoordinator>& coordinator, const McpConsumerConfig& config
) {
  if (!coordinator) {
    return 0;
  }
  return coordinator->EnsureConsumer(config);
}

} // namespace nodes
} // namespace mcp
} // namespace giga_edge
