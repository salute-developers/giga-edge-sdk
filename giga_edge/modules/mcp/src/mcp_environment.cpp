#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {

McpServiceEnvironment::McpServiceEnvironment(McpServiceConfig service_config)
  : config(std::move(service_config))
  , service(CreateMcpService(config))
  , coordinator(CreateMcpCoordinator(service)) {}

McpServiceEnvironment::~McpServiceEnvironment() {
  service->Shutdown();
}

McpServiceConfig ParseMcpServiceConfig(const YAML::Node& config) {
  McpServiceConfig result;
  if (config) {
    result.protocol_version = config["protocol_version"].as<std::string>(result.protocol_version);
    result.client_name = config["client_name"].as<std::string>(result.client_name);
    result.client_version = config["client_version"].as<std::string>(result.client_version);
    result.thread_pool_size = config["thread_pool_size"].as<uint32_t>(result.thread_pool_size);
    result.default_request_timeout_ms = config["default_request_timeout_ms"].as<uint32_t>(
      result.default_request_timeout_ms
    );
    result.default_tool_call_timeout_ms = config["default_tool_call_timeout_ms"].as<uint32_t>(
      result.default_tool_call_timeout_ms
    );
    result.default_read_resource_timeout_ms = config["default_read_resource_timeout_ms"].as<uint32_t>(
      result.default_read_resource_timeout_ms
    );
    result.default_ping_timeout_ms = config["default_ping_timeout_ms"].as<uint32_t>(result.default_ping_timeout_ms);
    result.debug_enabled = config["debug_enabled"].as<bool>(result.debug_enabled);
    const auto hydration = config["resource_hydration"];
    if (hydration) {
      result.resource_hydration.read_resources_on_list = hydration["read_resources_on_list"].as<bool>(
        result.resource_hydration.read_resources_on_list
      );
      result.resource_hydration.subscribe_to_subscribable_resources
        = hydration["subscribe_to_subscribable_resources"].as<bool>(
          result.resource_hydration.subscribe_to_subscribable_resources
        );
      result.resource_hydration.preferred_read_resource_match_phrases
        = hydration["preferred_read_resource_match_phrases"].as<std::vector<std::string>>(
          result.resource_hydration.preferred_read_resource_match_phrases
        );
      result.resource_hydration.retry_initial_ms = hydration["retry_initial_ms"].as<uint32_t>(
        result.resource_hydration.retry_initial_ms
      );
      result.resource_hydration.retry_max_ms = hydration["retry_max_ms"].as<uint32_t>(
        result.resource_hydration.retry_max_ms
      );
      result.resource_hydration.no_retry_error_codes = hydration["no_retry_error_codes"].as<std::vector<int32_t>>(
        result.resource_hydration.no_retry_error_codes
      );
    }
  }
  return result;
}

} // namespace mcp
} // namespace giga_edge
