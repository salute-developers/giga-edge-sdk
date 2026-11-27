#pragma once

#include "giga_edge/lib/runtime_manager/capability.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/personalization/capability.h"

namespace giga_edge {

class IPersonalizationCapabilityInternal : public RuntimeCapabilityBase, public IPersonalizationCapability {
public:
  explicit IPersonalizationCapabilityInternal(RuntimeManager& manager)
    : RuntimeCapabilityBase(manager) {}

  virtual Personalization GetPersonalization() const = 0;
};

} // namespace giga_edge
