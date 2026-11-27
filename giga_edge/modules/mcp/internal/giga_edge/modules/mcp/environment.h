#pragma once

#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/service.h"

#include <memory>

namespace giga_edge {
namespace mcp {

struct McpServiceEnvironment : ::giga_edge::Environment {
  McpServiceConfig config;
  std::shared_ptr<IMcpService> service;
  std::shared_ptr<IMcpCoordinator> coordinator;

  explicit McpServiceEnvironment(McpServiceConfig service_config);
  ~McpServiceEnvironment() override;
};

McpServiceConfig ParseMcpServiceConfig(const YAML::Node& config);

} // namespace mcp
} // namespace giga_edge

REGISTER_ENVIRONMENT(
  mcp,
  [](const YAML::Node& node) -> std::shared_ptr<void> {
    return std::make_shared<::giga_edge::mcp::McpServiceConfig>(::giga_edge::mcp::ParseMcpServiceConfig(node));
  },
  [](const std::shared_ptr<void>& config) -> ::giga_edge::EnvironmentPtr {
    return std::make_shared<::giga_edge::mcp::McpServiceEnvironment>(
      *std::static_pointer_cast<::giga_edge::mcp::McpServiceConfig>(config)
    );
  }
)
