#pragma once

#include "giga_edge/lib/runtime_manager/runtime_manager.h"

namespace giga_edge {

class RuntimeManager::SubmodelGraphBuilder {
public:
  using CurrentState = std::unordered_map<SubmodelVersionChain, SubmodelVersionState>;

  SubmodelGraphBuilder(
    const RuntimeManager& manager, Runtime& model, CurrentState* current_state, std::vector<BuildFailure>* failures
  );

  bool Add(
    std::vector<SubmodelVersionChain>& active_versions,
    CurrentState& active_current_state,
    const VersionToSwitch& version_to_switch
  );

private:
  bool AddDefaultVersion(
    std::vector<SubmodelVersionChain>& active_versions,
    CurrentState& active_current_state,
    const VersionToSwitch& version_to_switch
  );
  std::string CurrentVersion(const SubmodelVersionChain& version_chain) const;
  void Remove(const SubmodelVersionChain& version_chain);

  const RuntimeManager& manager_;
  Runtime& model_;
  CurrentState* current_state_;
  std::vector<BuildFailure>* failures_;
};

} // namespace giga_edge
