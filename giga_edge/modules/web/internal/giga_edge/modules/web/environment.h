#pragma once

#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/web/web_coordinator.h"
#include "giga_edge/modules/web/web_service.h"
#include "giga_edge/modules/web/web_types.h"

#include <memory>
#include <string>

namespace giga_edge {
namespace web {

struct WebServiceEnvironment : ::giga_edge::Environment {
  WebServiceConfig config;
  std::shared_ptr<IWebService> service;
  std::shared_ptr<IWebCoordinator> coordinator;

  explicit WebServiceEnvironment(WebServiceConfig service_config);
  ~WebServiceEnvironment() override;
};

WebServiceConfig ParseWebServiceConfig(const YAML::Node& config);

} // namespace web
} // namespace giga_edge

REGISTER_ENVIRONMENT(
  web,
  [](const YAML::Node& node) -> std::shared_ptr<void> {
    return std::make_shared<::giga_edge::web::WebServiceConfig>(::giga_edge::web::ParseWebServiceConfig(node));
  },
  [](const std::shared_ptr<void>& config) -> ::giga_edge::EnvironmentPtr {
    return std::make_shared<::giga_edge::web::WebServiceEnvironment>(
      *std::static_pointer_cast<::giga_edge::web::WebServiceConfig>(config)
    );
  }
)
