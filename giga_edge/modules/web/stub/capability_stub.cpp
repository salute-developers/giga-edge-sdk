#include "giga_edge/modules/web/capability.h"

namespace giga_edge {
namespace web {

std::shared_ptr<IWebCapability> CreateWebCapability(RuntimeManager&, std::string) {
  return nullptr;
}

} // namespace web
} // namespace giga_edge
