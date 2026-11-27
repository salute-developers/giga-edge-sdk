#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {

enum class FeatureSetType { CLIENT = 0, CAPABILITY };

class RuntimeManager;
struct UpdateAction;

class FeatureManager {
public:
  explicit FeatureManager(std::unordered_set<std::string> initial_features);

  void SetFeatures(std::unordered_set<std::string> features, const FeatureSetType feature_set_type);

  UpdateAction AddFeature(RuntimeManager& manager, const std::string& feature, const FeatureSetType feature_set_type);
  UpdateAction RemoveFeature(
    RuntimeManager& manager, const std::string& feature, const FeatureSetType feature_set_type
  );
  UpdateAction SetFeatures(
    RuntimeManager& manager, const std::unordered_set<std::string>& features, const FeatureSetType feature_set_type
  );

  const std::unordered_set<std::string>& GetFeatures(const FeatureSetType feature_set_type) const;
  const std::unordered_set<std::string>& GetFeatures() const;

private:
  void ValidateFeature(const std::string& feature, const FeatureSetType feature_set_type) const;
  void UpdateFeatures();

private:
  std::unordered_map<FeatureSetType, std::unordered_set<std::string>> features_per_type_;
  std::unordered_set<std::string> features_;
};

} // namespace giga_edge
