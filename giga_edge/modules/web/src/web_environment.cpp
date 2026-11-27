#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/web/environment.h"

#include <cstdint>

namespace giga_edge {
namespace web {

WebServiceEnvironment::WebServiceEnvironment(WebServiceConfig service_config)
  : config(service_config)
  , service(CreateWebService(config))
  , coordinator(CreateWebCoordinator(service)) {}

// The service threads are stopped while the environment still holds the service and the coordinator: the last
// reference will not end up on a pool thread or the watchdog thread.
WebServiceEnvironment::~WebServiceEnvironment() {
  service->Shutdown();
}

WebServiceConfig ParseWebServiceConfig(const YAML::Node& config) {
  WebServiceConfig result;
  if (config) {
    result.thread_pool_size = config["thread_pool_size"].as<uint32_t>(result.thread_pool_size);
    result.response_idle_timeout_ms = config["response_idle_timeout_ms"].as<uint32_t>(result.response_idle_timeout_ms);
    result.debug_enabled = config["debug_enabled"].as<bool>(result.debug_enabled);
  }
  return result;
}

} // namespace web
} // namespace giga_edge
