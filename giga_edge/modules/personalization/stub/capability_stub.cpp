#include "giga_edge/modules/personalization/capability.h"

namespace giga_edge {

PersonalizationCapabilityPtr CreatePersonalizationCapability(RuntimeManager&, PersonalizationEnvironmentPtr) {
  return nullptr;
}

CapabilityEnvironmentProviderPtr CreatePersonalizationEnvironmentProvider(PersonalizationEnvironmentPtr) {
  return nullptr;
}

} // namespace giga_edge
