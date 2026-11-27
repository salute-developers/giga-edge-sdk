#pragma once

#include <string>
#include <vector>

namespace giga_edge {

class RuntimeManager;
struct UpdateAction;

class RuntimeCapabilityBase {
public:
  explicit RuntimeCapabilityBase(RuntimeManager& manager);
  virtual ~RuntimeCapabilityBase() = default;

  // Hooks run synchronously after the manager has released its capability lock.
  // Must not change parts or features: the manager removes the listed parts right after the hook.
  virtual void OnBeforePartsRemoved(const std::vector<std::string>& part_ids) {}
  // May update the manager, but must not synchronously trigger an unbounded callback recursion.
  virtual void OnAfterStateUpdated(const UpdateAction& action) {}

protected:
  void AddFeature(const std::string& feature);
  void RemoveFeature(const std::string& feature);

protected:
  RuntimeManager& mgr_;
};

} // namespace giga_edge
