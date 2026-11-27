#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/types.h"
#include "giga_edge/lib/statistics/statistics.h"
#include "giga_edge/lib/submodel_version/submodel_version.h"

#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace giga_edge {

struct SubmodelParams;
class NodeBase;
using NodePtr = std::shared_ptr<NodeBase>;
struct NodeBaseConfig;
struct ILoopScheduler;

enum class SubmodelRequireMode {
  NONE,
  IMMEDIATE, // require only direct submodels
  SELECTIVE, // require direct submodels and their children according to their config
  RECURSIVE  // require all reachable submodels
};

struct SubmodelSignature {
  std::string id;
  std::unordered_map<std::string, std::string> inputs;
  std::unordered_map<std::string, std::string> outputs;
};

struct SubmodelFeature {
  std::unordered_set<std::string> feature_ids;
  std::string submodel_id;
  std::string submodel_version;
  SubmodelRequireMode require_mode = SubmodelRequireMode::NONE;
};

struct DefaultSubmodel {
  std::string submodel_version;
  SubmodelRequireMode require_mode = SubmodelRequireMode::NONE;
};

class RuntimeSubmodel;
using RuntimeSubmodelPtr = std::shared_ptr<RuntimeSubmodel>;

class RuntimeSubmodelVersion;
using RuntimeSubmodelVersionPtr = std::shared_ptr<RuntimeSubmodelVersion>;

class RuntimeSubmodel : public std::enable_shared_from_this<RuntimeSubmodel> {
public:
  RuntimeSubmodel(
    const SubmodelVersionChain& version_chain,
    std::unordered_map<std::string, NodeOutputPtr> inputs,
    std::unordered_map<std::string, NodeOutputPtr> outputs
  );

  RuntimeSubmodelPtr Submodel(const SubmodelVersionChain& version_chain);
  RuntimeSubmodelVersionPtr SubmodelVersion(const SubmodelVersionChain& version_chain);

  void CollectNodes(std::vector<NodePtr>& nodes) const;
  void SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler);
  bool Empty() const;

  const std::unordered_map<std::string, NodeOutputPtr>& Inputs();
  const std::unordered_map<std::string, NodeOutputPtr>& Outputs();

  void AddVersion(const std::string& version, std::shared_ptr<RuntimeSubmodelVersion> submodel_version);
  // Removal of a version in two steps, so that the graph is not stopped: VersionNodes reads the version tree (call
  // it under the runtime lock), DetachNodes waits for each node to become inactive and idle (call it outside).
  std::vector<NodePtr> VersionNodes(const std::string& version) const;
  void DetachNodes(const std::vector<NodePtr>& nodes) const;
  void RemoveVersion(const std::string& version);

  void GatherRequiredVersions(
    std::vector<SubmodelVersionChain>& result,
    const std::string& submodel_version,
    const std::unordered_set<std::string>& current_features
  ) const;

  // The nodes of the subtree, upstream first: the order in which they are detached on removal.
  void CollectNodesTopsorted(std::vector<NodePtr>& nodes) const;

  void WriteDot(
    std::ostream& subgraphs_out,
    std::ostream& edges_out,
    std::unordered_map<NodeOutputPtr, std::string>& node_output_names
  ) const;

private:
  RuntimeSubmodelVersionPtr SubmodelVersion(const std::string& version) const;

private:
  SubmodelVersionChain version_chain_;

  std::unordered_map<std::string, NodeOutputPtr> inputs_;
  std::unordered_map<std::string, NodeOutputPtr> outputs_;

  std::unordered_map<std::string, RuntimeSubmodelVersionPtr> versions_;
};

class RuntimeSubmodelVersion : public std::enable_shared_from_this<RuntimeSubmodelVersion> {
public:
  RuntimeSubmodelVersion(
    const NodeBaseConfig& node_base_config,
    const std::unordered_map<NodeOutputId, NodeOutputPtr>& inputs,
    const std::unordered_map<NodeOutputId, NodeOutputPtr>& outputs,
    const SubmodelParams& params,
    const IDataReaderPtr& data_reader
  );

  void Start();

  RuntimeSubmodelPtr Submodel(const SubmodelVersionChain& version_chain);
  RuntimeSubmodelVersionPtr SubmodelVersion(const SubmodelVersionChain& version_chain);

  bool HasOutput(const NodeOutputId& output_id) const;
  NodeOutputPtr NodeOutput(const NodeOutputId& output_id) const;

  void CollectNodes(std::vector<NodePtr>& nodes) const;
  void SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler);
  bool Empty() const;

  // The nodes of the subtree, upstream first: the order in which they are detached on removal.
  void CollectNodesTopsorted(std::vector<NodePtr>& nodes) const;

  void GatherRequiredVersions(
    std::vector<SubmodelVersionChain>& result, const std::unordered_set<std::string>& current_features
  );
  const std::unordered_map<std::string, DefaultSubmodel>& DefaultSubmodels() const;
  // The environments visible in this version: those of the version it is nested in and its own.
  const EnvironmentsMap& Environments() const;

  void WriteDot(
    std::ostream& subgraphs_out,
    std::ostream& edges_out,
    std::unordered_map<NodeOutputPtr, std::string>& node_output_names
  ) const;

private:
  RuntimeSubmodelPtr Submodel(const std::string& submodel_id) const;
  std::unordered_map<std::string, std::string> SubmodelsByFeatures(
    const std::unordered_set<std::string>& current_features
  );

private:
  SubmodelVersionChain version_chain_;

  std::unordered_map<std::string, DefaultSubmodel> default_submodels_;
  std::vector<SubmodelFeature> features_;

  std::vector<NodePtr> nodes_;
  std::unordered_map<NodeOutputId, NodeOutputPtr> node_outputs_;

  std::unordered_map<std::string, RuntimeSubmodelPtr> submodels_;

  std::unordered_map<NodeOutputId, NodeOutputPtr> inputs_;

  std::shared_ptr<EnvironmentsMap> environments_;
  bool started_;
};

} // namespace giga_edge
