#include "giga_edge/modules/mcp/capability.h"

namespace giga_edge {
namespace mcp {

std::shared_ptr<IMCPCapability> CreateMcpCapability(RuntimeManager&) {
  return nullptr;
}

} // namespace mcp
} // namespace giga_edge
