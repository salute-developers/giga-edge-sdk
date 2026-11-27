#include "giga_edge/modules/web/capability.h"

#include "giga_edge/lib/runtime_manager/capability.h"
#include "giga_edge/modules/web/environment.h"
#include "giga_edge/modules/web/web_service.h"

#include <iostream>
#include <utility>

namespace giga_edge {
namespace web {

namespace {

class WebCapability : public RuntimeCapabilityBase, public IWebCapability {
public:
  WebCapability(RuntimeManager& manager, std::string environment_name)
    : RuntimeCapabilityBase(manager)
    , environment_name_(std::move(environment_name)) {}

  void OnTransportAdded(const std::string& provider_id, WebTransportPtr transport) override {
    if (auto service = Service()) {
      service->AddTransport(provider_id, std::move(transport));
    }
  }

  void OnTransportRemoved(const std::string& provider_id) override {
    if (auto service = Service()) {
      service->RemoveTransport(provider_id);
    }
  }

  std::vector<std::string> GetProviderIds() const override {
    auto service = Service();
    return service ? service->GetProviderIds() : std::vector<std::string>{};
  }

private:
  std::shared_ptr<IWebService> Service() const {
    auto model = mgr_.Model();
    if (!model) {
      Warn("runtime model is not available");
      return nullptr;
    }
    auto env = model->GetEnvironment(environment_name_);
    if (!env) {
      Warn("no '" + environment_name_ + "' model environment");
      return nullptr;
    }
    auto web_env = std::dynamic_pointer_cast<WebServiceEnvironment>(env);
    if (!web_env) {
      Warn("'" + environment_name_ + "' environment is not WebServiceEnvironment");
      return nullptr;
    }
    if (!web_env->service) {
      Warn("'" + environment_name_ + "' environment is not initialized");
      return nullptr;
    }
    return web_env->service;
  }

  void Warn(const std::string& reason) const {
    if (last_warning_ == reason) {
      return;
    }
    last_warning_ = reason;
    std::cerr << "[WARNING] Web capability is disabled: " << reason << "." << std::endl;
  }

  std::string environment_name_;
  mutable std::string last_warning_;
};

} // namespace

std::shared_ptr<IWebCapability> CreateWebCapability(RuntimeManager& manager, std::string environment_name) {
  return std::make_shared<WebCapability>(manager, std::move(environment_name));
}

} // namespace web
} // namespace giga_edge
