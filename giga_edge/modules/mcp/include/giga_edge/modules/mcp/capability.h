#pragma once

#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/mcp/connection.h"

#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace mcp {

class IMCPCapability {
public:
  virtual ~IMCPCapability() = default;

  virtual void OnConnectionAdded(const std::string& server_id, ConnectionPtr connection) = 0;
  virtual void OnConnectionRemoved(const std::string& server_id) = 0;
  virtual std::vector<std::string> GetServerIds() const = 0;
};

std::shared_ptr<IMCPCapability> CreateMcpCapability(RuntimeManager& manager);

} // namespace mcp
} // namespace giga_edge
