#pragma once

#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/lib/runtime_data/runtime_data.h"
#include "giga_edge/lib/runtime_manager/capability.h"
#include "giga_edge/lib/runtime_manager/feature_manager.h"

#include <cassert>
#include <mutex>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace giga_edge {

struct SubmodelMeta {
  std::string version_id;
  std::optional<std::string> config_version;
};

class RuntimeManager : public NonCopyable {
public:
  explicit RuntimeManager(
    const RuntimePart& core,
    const std::vector<RuntimePart>& parts = {},
    EnvironmentsMap environments = {},
    std::unordered_set<std::string> features = {}
  );

  UpdateAction InitialUpdateState();

  std::shared_ptr<Runtime> Model() const;
  // Creates an independent runtime with all dependencies of a version available in current parts.
  // The version config must be a complete runtime root config. Its environments are isolated.
  std::shared_ptr<Runtime> CreateRuntime(const std::string& version_id) const;

  const std::unordered_set<std::string>& Features() const;
  UpdateAction SetFeatures(const std::unordered_set<std::string>& features);
  UpdateAction AddFeature(const std::string& feature);
  UpdateAction RemoveFeature(const std::string& feature);

  UpdateAction AddRuntimeParts(const std::vector<RuntimePart>& parts);
  UpdateAction RemoveRuntimeParts(const std::vector<std::string>& part_ids);

  // Expands the passed roots using the specified mode. A missing part has no config_version.
  std::optional<std::vector<SubmodelMeta>> CollectSubmodelsMeta(
    const std::vector<SubmodelVersionChain>& roots,
    SubmodelRequireMode root_require_mode = SubmodelRequireMode::RECURSIVE
  );

  UpdateAction SetState(const std::vector<RuntimePart>& parts, const std::unordered_set<std::string>& features);
  // Treat passed versions as roots and expand their submodels in Selective mode.
  std::unordered_set<std::string> GetMissingSubmodelVersions(const std::unordered_set<std::string>& versions);
  // Treat passed chains as required model edges and expand the leaf with its parent edge require_mode.
  std::unordered_set<std::string> GetMissingSubmodelVersions(const std::vector<SubmodelVersionChain>& version_chains);

  std::vector<std::string> GetPartIds() const;
  bool HasPart(const std::string& part_id) const;
  // The part exists and its data is readable: for a deferred part, the configs of all its versions are read.
  bool IsPartReady(const std::string& part_id);
  std::optional<std::string> GetVersionPart(const std::string& version_id) const;
  ILazyDataReaderPtr GetDataReader(const std::string& part_id);
  FeatureManager& GetFeatureManager();

  template<class T>
  std::shared_ptr<T> GetCapability() const {
    return std::dynamic_pointer_cast<T>(GetCapabilityByType(typeid(T)));
  }

  template<class T>
  bool HasCapability() const {
    return static_cast<bool>(GetCapabilityByType(typeid(T)));
  }

  template<class T>
  void RegisterCapability(std::shared_ptr<T> capability) {
    if (!capability) {
      return;
    }
    const auto base = std::dynamic_pointer_cast<RuntimeCapabilityBase>(capability);
    assert(base != nullptr);

    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    capabilities_[std::type_index(typeid(T))] = base;
  }

private:
  struct UsableSubmodel {
    std::string submodel_id;
    std::string submodel_version;
    SubmodelRequireMode require_mode;
  };

  using UsableSubmodels = std::vector<UsableSubmodel>;

  struct SubmodelProperties {
    std::string config_version;
    UsableSubmodels usable_submodels;
    AvailableFeatures feature_ids;
  };

  struct RuntimePartStatus {
    ILazyDataReaderPtr data_reader;
    std::unordered_map<std::string, SubmodelVersion> available_versions;
    std::unordered_map<std::string, SubmodelProperties> submodel_properties;
    bool permanent = false; // a part that cannot be deleted, for example one shipped with the firmware
    bool deferred = false;
  };

  struct SubmodelVersionState {
    std::string current_version;
    std::string required_version;

    bool operator==(const SubmodelVersionState& other);
    bool operator!=(const SubmodelVersionState& other);
  };

  struct VersionToSwitch {
    SubmodelVersionChain version_chain;
    std::string version_from;
    SubmodelVersionState version_to;
  };

  struct BuildFailure {
    SubmodelVersionChain version_chain;
    // The version the slot requires: the fallback one was built if its part is missing.
    std::string required_version;
  };

  struct SubmodelRoot {
    std::string submodel_version;
    SubmodelRequireMode require_mode;
  };

  class SubmodelGraphBuilder;

  using SubmodelRoots = std::vector<SubmodelRoot>;

  friend class FeatureManager;

  UpdateAction UpdateState(bool free_unused = true);
  void NotifyBeforePartsRemoved(const std::vector<std::string>& part_ids);
  void NotifyAfterStateUpdated(const UpdateAction& action);
  std::vector<std::shared_ptr<RuntimeCapabilityBase>> GetCapabilities() const;
  const SubmodelProperties& GetSubmodelProperties(const std::string& version_id);
  const SubmodelProperties* TryGetSubmodelProperties(const std::string& version_id);
  SubmodelRequireMode GetSubmodelRequireMode(const SubmodelVersionChain& version_chain);
  std::vector<SubmodelMeta> CollectSubmodelsMeta(
    const SubmodelRoots& roots, std::unordered_set<std::string> collected_versions
  );
  std::vector<SubmodelMeta> CollectSubmodelsMeta(
    const std::vector<SubmodelVersionChain>& roots, std::optional<SubmodelRequireMode> root_require_mode
  );
  AvailableFeatures CollectAvailableFeatures();

  void AddRuntimePartsNoUpdate(const std::vector<RuntimePart>& parts);
  void RemovePart(const std::string& part_id);

  bool AddSubmodelVersion(
    std::vector<SubmodelVersionChain>& active_versions,
    std::unordered_map<SubmodelVersionChain, SubmodelVersionState>& active_current_state,
    std::vector<BuildFailure>& failures,
    const VersionToSwitch& version_to_switch
  );
  void SetActiveSubmodelVersions(
    const std::vector<SubmodelVersionChain>& active_versions,
    const std::unordered_map<SubmodelVersionChain, SubmodelVersionState>& active_current_state
  );
  void RemoveSubmodelVersion(const SubmodelVersionChain& version_chain);
  void SwitchVersions(const std::vector<VersionToSwitch>& versions_to_switch, std::vector<BuildFailure>& failures);

  std::shared_ptr<RuntimeCapabilityBase> GetCapabilityByType(const std::type_info& t) const {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    auto it = capabilities_.find(t);
    if (it == capabilities_.end()) {
      return {};
    }
    return it->second;
  }

private:
  std::shared_ptr<Runtime> runtime_;

  FeatureManager feature_manager_;
  std::unordered_map<SubmodelVersionChain, SubmodelVersionState> current_state_;
  // Submodel versions are built in parallel: current_state_ is guarded by this mutex during the build.
  mutable std::mutex build_state_mutex_;

  std::unordered_map<std::string, RuntimePartStatus> runtime_parts_;
  std::unordered_map<std::string, std::string> version_to_part_;

  mutable std::mutex capabilities_mutex_;
  std::unordered_map<std::type_index, std::shared_ptr<RuntimeCapabilityBase>> capabilities_;
};

using RuntimeManagerPtr = std::shared_ptr<RuntimeManager>;

class ICapabilityEnvironmentProvider {
public:
  virtual ~ICapabilityEnvironmentProvider() = default;
  virtual void RegisterEnvironments(EnvironmentsMap& environments) const = 0;
};

using CapabilityEnvironmentProviderPtr = std::shared_ptr<ICapabilityEnvironmentProvider>;

} // namespace giga_edge
