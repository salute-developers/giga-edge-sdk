#include "giga_edge/modules/personalization/capability.h"

#include "giga_edge/modules/personalization/capability_internal.h"

#include <memory>
#include <utility>

namespace giga_edge {

namespace {

class PersonalizationCapabilityImpl : public IPersonalizationCapabilityInternal {
public:
  PersonalizationCapabilityImpl(RuntimeManager& manager, PersonalizationEnvironmentPtr environment)
    : IPersonalizationCapabilityInternal(manager)
    , environment_(environment ? std::move(environment) : std::make_shared<PersonalizationEnvironment>()) {}

  // IPersonalizationCapability.
  void SetPronoun(const PersonalizationPronoun& pronoun) override {
    environment_->SetPronoun(pronoun);
  }

  void SetVoice(const std::string& voice) override {
    environment_->SetVoice(voice);
  }

  // IPersonalizationCapabilityInternal.
  Personalization GetPersonalization() const override {
    return environment_->Get().personalization;
  }

private:
  const PersonalizationEnvironmentPtr environment_;
};

class PersonalizationEnvironmentProvider final : public ICapabilityEnvironmentProvider {
public:
  explicit PersonalizationEnvironmentProvider(PersonalizationEnvironmentPtr environment)
    : environment_(std::move(environment)) {}

  void RegisterEnvironments(EnvironmentsMap& environments) const override {
    if (environment_) {
      environments[kPersonalizationEnvironmentName] = environment_;
    }
  }

private:
  const PersonalizationEnvironmentPtr environment_;
};

} // namespace

PersonalizationCapabilityPtr CreatePersonalizationCapability(
  RuntimeManager& manager, PersonalizationEnvironmentPtr environment
) {
  return std::make_shared<PersonalizationCapabilityImpl>(manager, std::move(environment));
}

CapabilityEnvironmentProviderPtr CreatePersonalizationEnvironmentProvider(PersonalizationEnvironmentPtr environment) {
  return std::make_shared<PersonalizationEnvironmentProvider>(std::move(environment));
}

} // namespace giga_edge
