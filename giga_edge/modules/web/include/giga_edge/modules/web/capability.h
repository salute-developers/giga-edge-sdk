#pragma once

#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/web/web_transport.h"

#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {

class IWebCapability {
public:
  virtual ~IWebCapability() = default;

  virtual void OnTransportAdded(const std::string& provider_id, WebTransportPtr transport) = 0;
  virtual void OnTransportRemoved(const std::string& provider_id) = 0;
  virtual std::vector<std::string> GetProviderIds() const = 0;
};

std::shared_ptr<IWebCapability> CreateWebCapability(RuntimeManager& manager, std::string environment_name = "web");

} // namespace web
} // namespace giga_edge
