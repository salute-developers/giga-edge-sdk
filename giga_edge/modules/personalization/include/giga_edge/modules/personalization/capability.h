#pragma once

#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/personalization/capability_type.h"
#include "giga_edge/modules/personalization/environment.h"

#include <memory>
#include <string>

namespace giga_edge {

class IPersonalizationCapability {
public:
  virtual ~IPersonalizationCapability() = default;

  virtual void SetPronoun(const PersonalizationPronoun& pronoun) = 0;

  virtual void SetVoice(const std::string& voice) = 0;
};

using PersonalizationCapabilityPtr = std::shared_ptr<IPersonalizationCapability>;

// environment is the one the provider gave to the graph; without it only the capability itself sees the values.
PersonalizationCapabilityPtr CreatePersonalizationCapability(
  RuntimeManager& manager, PersonalizationEnvironmentPtr environment = nullptr
);

CapabilityEnvironmentProviderPtr CreatePersonalizationEnvironmentProvider(PersonalizationEnvironmentPtr environment);

} // namespace giga_edge
