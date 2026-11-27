#include "giga_edge/modules/mcp/capability.h"

#include "giga_edge/lib/runtime_manager/capability.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/log.h"

#include <utility>

namespace giga_edge {
namespace mcp {

namespace {

class MCPCapability : public RuntimeCapabilityBase, public IMCPCapability {
public:
  explicit MCPCapability(RuntimeManager& manager)
    : RuntimeCapabilityBase(manager) {
    auto model = mgr_.Model();
    if (!model) {
      detail::WriteMcpLog(std::string("[WARNING] MCP capability is disabled: runtime model is not available."));
      return;
    }

    auto env = model->GetEnvironment("mcp");
    if (!env) {
      detail::WriteMcpLog(std::string("[WARNING] MCP capability is disabled: no 'mcp' model environment."));
      return;
    }
    auto mcp_env = std::dynamic_pointer_cast<McpServiceEnvironment>(env);
    if (!mcp_env) {
      detail::WriteMcpLog(
        std::string("[WARNING] MCP capability is disabled: 'mcp' environment is not McpServiceEnvironment.")
      );
      return;
    }

    service_ = mcp_env->service;
    coordinator_ = mcp_env->coordinator;
    if (!service_ || !coordinator_) {
      detail::WriteMcpLog(std::string("[WARNING] MCP capability is disabled: 'mcp' environment is not initialized."));
    }
  }

  void OnConnectionAdded(const std::string& server_id, ConnectionPtr connection) override {
    if (!service_) {
      return;
    }
    service_->AddServer(server_id, std::move(connection));
  }

  void OnConnectionRemoved(const std::string& server_id) override {
    if (!service_) {
      return;
    }
    service_->RemoveServer(server_id);
  }

  std::vector<std::string> GetServerIds() const override {
    if (!service_) {
      return {};
    }
    return service_->GetServerIds();
  }

private:
  std::shared_ptr<IMcpService> service_;
  std::shared_ptr<IMcpCoordinator> coordinator_;
};

} // namespace

std::shared_ptr<IMCPCapability> CreateMcpCapability(RuntimeManager& manager) {
  return std::make_shared<MCPCapability>(manager);
}

} // namespace mcp
} // namespace giga_edge
