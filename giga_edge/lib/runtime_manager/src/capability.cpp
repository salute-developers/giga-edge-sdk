#include "giga_edge/lib/runtime_manager/capability.h"

#include "giga_edge/lib/runtime_manager/feature_manager.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"

namespace giga_edge {

RuntimeCapabilityBase::RuntimeCapabilityBase(RuntimeManager& manager)
  : mgr_(manager) {}

void RuntimeCapabilityBase::AddFeature(const std::string& feature) {
  mgr_.GetFeatureManager().AddFeature(mgr_, feature, FeatureSetType::CAPABILITY);
}

void RuntimeCapabilityBase::RemoveFeature(const std::string& feature) {
  mgr_.GetFeatureManager().RemoveFeature(mgr_, feature, FeatureSetType::CAPABILITY);
}

} // namespace giga_edge
