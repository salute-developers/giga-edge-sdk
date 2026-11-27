#include "giga_edge/lib/runtime_manager/feature_manager.h"

#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"

#include <sstream>
#include <stdexcept>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(giga_edge::FeatureSetType, "client", "capability");

namespace giga_edge {

FeatureManager::FeatureManager(std::unordered_set<std::string> initial_features) {
  features_per_type_[FeatureSetType::CLIENT] = std::move(initial_features);
  features_per_type_[FeatureSetType::CAPABILITY] = {};
  UpdateFeatures();
}

void FeatureManager::SetFeatures(std::unordered_set<std::string> features, const FeatureSetType feature_set_type) {
  for (const auto& feature : features) {
    ValidateFeature(feature, feature_set_type);
  }

  features_per_type_[feature_set_type] = std::move(features);
  UpdateFeatures();
}

void FeatureManager::ValidateFeature(const std::string& feature, const FeatureSetType feature_set_type) const {
  if (
    features_.count(feature) > 0
    && (features_per_type_.count(feature_set_type) == 0 || features_per_type_.at(feature_set_type).count(feature) == 0)
  ) {
    std::stringstream ss;
    ss << "Feature \'" << feature << "\' of \'" << EnumToString(feature_set_type)
       << "\' feature set already exists in another feature set";
    throw std::runtime_error(ss.str());
  }
}

UpdateAction FeatureManager::AddFeature(
  RuntimeManager& manager, const std::string& feature, const FeatureSetType feature_set_type
) {
  ValidateFeature(feature, feature_set_type);

  features_per_type_[feature_set_type].insert(feature);
  UpdateFeatures();
  return manager.UpdateState();
}

UpdateAction FeatureManager::RemoveFeature(
  RuntimeManager& manager, const std::string& feature, const FeatureSetType feature_set_type
) {
  features_per_type_[feature_set_type].erase(feature);
  UpdateFeatures();
  return manager.UpdateState();
}

UpdateAction FeatureManager::SetFeatures(
  RuntimeManager& manager, const std::unordered_set<std::string>& features, const FeatureSetType feature_set_type
) {
  SetFeatures(features, feature_set_type);
  return manager.UpdateState();
}

const std::unordered_set<std::string>& FeatureManager::GetFeatures(const FeatureSetType feature_set_type) const {
  // Safety net for a future enum extension
  if (features_per_type_.find(feature_set_type) == features_per_type_.end()) {
    throw std::runtime_error(
      "Attempt to get uninitialized feature set for type: " + std::string{EnumToString(feature_set_type)}
    );
  }
  return features_per_type_.at(feature_set_type);
}

const std::unordered_set<std::string>& FeatureManager::GetFeatures() const {
  return features_;
}

void FeatureManager::UpdateFeatures() {
  features_.clear();
  for (const auto& [set_type, features] : features_per_type_) {
    features_.insert(features.begin(), features.end());
  }
}

} // namespace giga_edge
