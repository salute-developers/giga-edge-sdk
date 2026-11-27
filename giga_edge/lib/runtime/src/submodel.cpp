#include "giga_edge/lib/runtime/submodel.h"

#include "giga_edge/lib/common/overloaded.h"
#include "giga_edge/lib/common/stderr_line.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/params.h"

#include <iostream>
#include <stdexcept>
#include <variant>

namespace giga_edge {

RuntimeSubmodel::RuntimeSubmodel(
  const SubmodelVersionChain& version_chain,
  std::unordered_map<std::string, NodeOutputPtr> inputs,
  std::unordered_map<std::string, NodeOutputPtr> outputs
)
  : version_chain_(version_chain)
  , inputs_(inputs)
  , outputs_(outputs) {}

RuntimeSubmodelPtr RuntimeSubmodel::Submodel(const SubmodelVersionChain& version_chain) {
  if (version_chain.ids_chain.size() == 1) {
    return shared_from_this();
  }

  return SubmodelVersion(version_chain.ids_chain[0].version)->Submodel(version_chain.PopFront());
}

RuntimeSubmodelVersionPtr RuntimeSubmodel::SubmodelVersion(const SubmodelVersionChain& version_chain) {
  return SubmodelVersion(version_chain.ids_chain[0].version)->SubmodelVersion(version_chain.PopFront());
}

void RuntimeSubmodel::CollectNodes(std::vector<NodePtr>& nodes) const {
  for (auto& [_, submodel_version] : versions_) {
    submodel_version->CollectNodes(nodes);
  }
}

void RuntimeSubmodel::SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler) {
  for (auto& [_, submodel_version] : versions_) {
    submodel_version->SetLoopScheduler(scheduler);
  }
}

bool RuntimeSubmodel::Empty() const {
  for (auto& [_, submodel_version] : versions_) {
    if (!submodel_version->Empty()) {
      return false;
    }
  }
  return true;
}

const std::unordered_map<std::string, NodeOutputPtr>& RuntimeSubmodel::Inputs() {
  return inputs_;
}

const std::unordered_map<std::string, NodeOutputPtr>& RuntimeSubmodel::Outputs() {
  return outputs_;
}

void RuntimeSubmodel::AddVersion(const std::string& version, std::shared_ptr<RuntimeSubmodelVersion> submodel_version) {
  if (versions_.find(version) != versions_.end()) {
    throw std::invalid_argument(
      "Submodel version " + version + " in submodel " + version_chain_.AsString() + " already exists"
    );
  }

  versions_.try_emplace(version, std::move(submodel_version));
}

std::vector<NodePtr> RuntimeSubmodel::VersionNodes(const std::string& version) const {
  std::vector<NodePtr> nodes;
  SubmodelVersion(version)->CollectNodesTopsorted(nodes);
  return nodes;
}

void RuntimeSubmodel::DetachNodes(const std::vector<NodePtr>& nodes) const {
  std::unordered_set<NodeOutputPtr> outputs_to_unsubscribe;
  for (const auto& [_, input] : inputs_) {
    outputs_to_unsubscribe.insert(input);
  }
  for (const auto& [_, output] : outputs_) {
    outputs_to_unsubscribe.insert(output);
  }
  for (const auto& node : nodes) {
    node->UnsubscribeExternalInputs(outputs_to_unsubscribe);
  }
}

void RuntimeSubmodel::RemoveVersion(const std::string& version) {
  versions_.erase(version);
}

void RuntimeSubmodel::GatherRequiredVersions(
  std::vector<SubmodelVersionChain>& result,
  const std::string& submodel_version,
  const std::unordered_set<std::string>& current_features
) const {
  if (auto version_it = versions_.find(submodel_version); version_it != versions_.end()) {
    version_it->second->GatherRequiredVersions(result, current_features);
  }
}

void RuntimeSubmodel::CollectNodesTopsorted(std::vector<NodePtr>& nodes) const {
  for (const auto& [_, submodel_version] : versions_) {
    submodel_version->CollectNodesTopsorted(nodes);
  }
}

RuntimeSubmodelVersionPtr RuntimeSubmodel::SubmodelVersion(const std::string& version) const {
  auto submodel_version = versions_.find(version);
  if (submodel_version == versions_.end()) {
    throw std::runtime_error("No submodel version " + version + " in submodel " + version_chain_.AsString());
  } else {
    return submodel_version->second;
  }
}

void RuntimeSubmodel::WriteDot(
  std::ostream& subgraphs_out,
  std::ostream& edges_out,
  std::unordered_map<NodeOutputPtr, std::string>& node_output_names
) const {
  subgraphs_out << "subgraph" << " \"" << version_chain_.AsString() << "\" {" << std::endl;
  subgraphs_out << "label = \"" << version_chain_.ids_chain.back().id << "\";" << std::endl;
  subgraphs_out << "color = green;" << std::endl;
  subgraphs_out << "cluster = true;" << std::endl;
  for (const auto& [_, version] : versions_) {
    version->WriteDot(subgraphs_out, edges_out, node_output_names);
  }
  for (const auto& [id, output] : outputs_) {
    subgraphs_out << node_output_names.at(output) << " [shape=box label=\"" << id << "\"]" << std::endl;
  }
  subgraphs_out << "}" << std::endl;
}

namespace {

EnvironmentsMap BuildSubmodelEnvironments(const NodeBaseConfig& node_base_config, const SubmodelParams& params) {
  if (node_base_config.environments == nullptr) {
    throw std::invalid_argument("NodeBaseConfig.environments must not be null");
  }

  EnvironmentsMap result = *node_base_config.environments;
  for (const auto& [env_name, env_params] : params.environments_map_params) {
    result.insert_or_assign(env_name, MakeEnvironment(env_params));
  }

  return result;
}

class RuntimeSubmodelVersionBuilder {
public:
  RuntimeSubmodelVersionBuilder(
    const NodeBaseConfig& node_base_config,
    const std::unordered_map<NodeOutputId, NodeOutputPtr>& inputs,
    const std::unordered_map<NodeOutputId, NodeOutputPtr>& outputs,
    const SubmodelParams& params,
    const IDataReaderPtr& data_reader
  )
    : node_base_config_(node_base_config)
    , submodel_inputs_(inputs)
    , submodel_outputs_(outputs)
    , params_(params)
    , data_reader_(data_reader) {
    ValidateGraphNames();
    InitOutputs(params_.nodes);
    InitOutputs(params_.submodels);
    ValidateOutputs();
    InitNodes();
    InitSubmodules();
    all_outputs.insert(inputs.begin(), inputs.end());
  }

private:
  std::string SubmodelFullId() const {
    return node_base_config_.submodel_version_chain.AsString();
  }

  std::string ErrorPrefix() const {
    return "Failed to init submodule version " + SubmodelFullId() + ". ";
  }

  void ValidateGraphNames() const {
    std::unordered_set<std::string> node_names;

    for (const auto& node_params : params_.nodes) {
      const auto& node_name = node_params.name;
      if (!node_names.emplace(node_name).second) {
        throw std::runtime_error(ErrorPrefix() + "Duplicate node name detected: '" + node_name + "'");
      }
    }
  }

  void InitOutput(const NodeOutputId& output_id) {
    NodeOutputPtr output = submodel_outputs_.find(output_id) == submodel_outputs_.end()
      ? std::make_shared<NodeOutput>(output_id)
      : submodel_outputs_.at(output_id);

    if (!all_outputs.try_emplace(output_id, output).second) {
      throw std::runtime_error(ErrorPrefix() + "Multiple outputs with id " + output->Id());
    }
  } // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): mismodeled libstdc++ shared_ptr internals

  template<class T>
  void InitOutputs(const T& items) {
    for (const auto& item : items) {
      for (const auto& [_, output_id] : item.outputs) {
        InitOutput(output_id);
      }
    }
  }

  void ValidateOutputs() {
    for (const auto& [output_id, _] : submodel_outputs_) {
      if (all_outputs.find(output_id) == all_outputs.end()) {
        throw std::runtime_error(ErrorPrefix() + "It does not provide output " + output_id);
      }
    }
  }

  std::unordered_map<NodeOutputId, NodeOutputPtr> FindNodeOutputs(
    const NodeId& node_id, const std::unordered_map<std::string, std::string>& mapping
  ) {
    std::unordered_map<NodeOutputId, NodeOutputPtr> result;

    for (const auto& [id, node_output_id] : mapping) {
      auto inputs_it = submodel_inputs_.find(node_output_id);
      auto all_outputs_it = all_outputs.find(node_output_id);

      if (inputs_it == submodel_inputs_.end() && all_outputs_it == all_outputs.end()) {
        throw std::runtime_error(ErrorPrefix() + "Cannot find node output " + node_output_id + " for " + node_id);
      } else if (inputs_it != submodel_inputs_.end() && all_outputs_it != all_outputs.end()) {
        throw std::runtime_error(
          ErrorPrefix() + node_output_id + " present both in submodel inputs and in node output"
        );
      } else if (inputs_it != submodel_inputs_.end()) {
        result.emplace(id, inputs_it->second);
      } else {
        result.emplace(id, all_outputs_it->second);
      }
    }
    return result;
  }

  std::unordered_set<NodeOutputId> FindExternalNodeInputs(const std::unordered_map<std::string, std::string>& mapping) {
    std::unordered_set<NodeOutputId> result;
    for (const auto& [id, node_output_id] : mapping) {
      if (submodel_inputs_.find(node_output_id) != submodel_inputs_.end()) {
        result.insert(id);
      }
    }
    return result;
  }

  void InitNodes() {
    for (const auto& node_params : params_.nodes) {
      auto node_config = node_base_config_;
      node_config.base_id = node_params.name;
      node_config.inputs = FindNodeOutputs(node_params.name, node_params.inputs);
      node_config.outputs = FindNodeOutputs(node_params.name, node_params.outputs);
      node_config.priority = node_params.priority;
      node_config.external_inputs = FindExternalNodeInputs(node_params.inputs);

      try {
        auto node = NodeRegistry::CreateNode(node_params.type, node_config, data_reader_, node_params.params);
        nodes.push_back(node);
      } catch (...) {
        WriteStderrLine(ErrorPrefix() + "Failed to create node " + node_params.name + "\n");
        throw;
      }
    }
  }

  void InitSubmodules() {
    for (const auto& submodel_params : params_.submodels) {
      auto inputs = FindNodeOutputs(submodel_params.id, submodel_params.inputs);
      auto outputs = FindNodeOutputs(submodel_params.id, submodel_params.outputs);
      auto submodel_version_chain = node_base_config_.submodel_version_chain;
      submodel_version_chain.ids_chain.push_back({.id = submodel_params.id});
      if (!submodels
             .try_emplace(
               submodel_params.id, std::make_shared<RuntimeSubmodel>(submodel_version_chain, inputs, outputs)
             )
             .second) {
        throw std::runtime_error(ErrorPrefix() + "Multiple submodels with id " + submodel_params.id);
      }
    }
  }

public:
  std::unordered_map<NodeOutputId, NodeOutputPtr> all_outputs;
  std::vector<NodePtr> nodes;
  std::unordered_map<std::string, RuntimeSubmodelPtr> submodels;

private:
  const NodeBaseConfig& node_base_config_;
  const std::unordered_map<NodeOutputId, NodeOutputPtr>& submodel_inputs_;
  const std::unordered_map<NodeOutputId, NodeOutputPtr>& submodel_outputs_;
  const SubmodelParams& params_;
  const IDataReaderPtr& data_reader_;
};

std::vector<std::variant<NodePtr, RuntimeSubmodelPtr>> Topsorted(
  const std::vector<NodePtr>& nodes, const std::unordered_map<std::string, RuntimeSubmodelPtr>& submodels
) {
  std::vector<std::variant<NodePtr, RuntimeSubmodelPtr>> result;
  std::unordered_map<NodeOutputPtr, std::vector<std::variant<NodePtr, RuntimeSubmodelPtr>>> inputs;

  for (const auto& node : nodes) {
    for (const auto& [input_id, input] : node->Inputs()) {
      inputs[input].emplace_back(node);
    }
  }

  for (const auto& [_, submodel] : submodels) {
    for (const auto& [input_id, input] : submodel->Inputs()) {
      inputs[input].emplace_back(submodel);
    }
  }

  std::unordered_set<std::variant<NodePtr, RuntimeSubmodelPtr>> visited;

  std::function<void(const std::variant<NodePtr, RuntimeSubmodelPtr>&)> dfs =
    [&](const std::variant<NodePtr, RuntimeSubmodelPtr>& item) -> void {
    if (visited.find(item) != visited.end()) {
      return;
    }
    visited.insert(item);
    std::visit(
      overloaded{
        [&](const NodePtr& node) {
          for (const auto& [output_id, output] : node->Outputs()) {
            if (node->IsRecursiveOutput(output_id)) {
              continue;
            }

            for (const auto& next_item : inputs[output]) {
              dfs(next_item);
            }
          }
        },
        [&](const RuntimeSubmodelPtr& submodel) {
          for (const auto& [output_id, output] : submodel->Outputs()) {
            for (const auto& next_item : inputs[output]) {
              dfs(next_item);
            }
          }
        }
      },
      item
    );
    result.push_back(item);
  };

  for (const auto& node : nodes) {
    dfs(node);
  }
  for (const auto& [_, submodel] : submodels) {
    dfs(submodel);
  }
  std::reverse(result.begin(), result.end());
  return result;
}

} // namespace

RuntimeSubmodelVersion::RuntimeSubmodelVersion(
  const NodeBaseConfig& node_base_config,
  const std::unordered_map<NodeOutputId, NodeOutputPtr>& inputs,
  const std::unordered_map<NodeOutputId, NodeOutputPtr>& outputs,
  const SubmodelParams& params,
  const IDataReaderPtr& data_reader
)
  : version_chain_(node_base_config.submodel_version_chain)
  , default_submodels_(params.default_submodels)
  , features_(params.features)
  , environments_(std::make_shared<EnvironmentsMap>(BuildSubmodelEnvironments(node_base_config, params)))
  , started_(false) {
  // Can't avoid this copying because we need to pass the environments map to the builder.
  NodeBaseConfig submodel_node_base_config = node_base_config;
  submodel_node_base_config.environments = environments_.get();

  RuntimeSubmodelVersionBuilder builder(submodel_node_base_config, inputs, outputs, params, data_reader);
  inputs_ = inputs;
  nodes_ = std::move(builder.nodes);
  node_outputs_ = std::move(builder.all_outputs);
  submodels_ = std::move(builder.submodels);
}

void RuntimeSubmodelVersion::Start() {
  if (started_) {
    return;
  }
  for (const auto& node : nodes_) {
    node->Start();
  }
  for (const auto& node : nodes_) {
    node->InitRecursiveOutputs();
  }
  started_ = true;
}

RuntimeSubmodelPtr RuntimeSubmodelVersion::Submodel(const SubmodelVersionChain& version_chain) {
  return Submodel(version_chain.ids_chain[0].id)->Submodel(version_chain);
}

RuntimeSubmodelVersionPtr RuntimeSubmodelVersion::SubmodelVersion(const SubmodelVersionChain& version_chain) {
  if (version_chain.ids_chain.empty()) {
    return shared_from_this();
  }
  return Submodel(version_chain.ids_chain[0].id)->SubmodelVersion(version_chain);
}

bool RuntimeSubmodelVersion::HasOutput(const NodeOutputId& output_id) const {
  return node_outputs_.find(output_id) != node_outputs_.end();
}

NodeOutputPtr RuntimeSubmodelVersion::NodeOutput(const NodeOutputId& output_id) const {
  auto node_output = node_outputs_.find(output_id);
  if (node_output == node_outputs_.end()) {
    throw std::runtime_error("No node output " + output_id + " in submodel version: " + version_chain_.AsString());
  } else {
    return node_output->second;
  }
}

const std::unordered_map<std::string, DefaultSubmodel>& RuntimeSubmodelVersion::DefaultSubmodels() const {
  return default_submodels_;
}

const EnvironmentsMap& RuntimeSubmodelVersion::Environments() const {
  return *environments_;
}

RuntimeSubmodelPtr RuntimeSubmodelVersion::Submodel(const std::string& submodel_id) const {
  auto submodel = submodels_.find(submodel_id);
  if (submodel == submodels_.end()) {
    throw std::runtime_error("No submodel " + submodel_id + " in submodel version: " + version_chain_.AsString());
  } else {
    return submodel->second;
  }
}

void RuntimeSubmodelVersion::CollectNodes(std::vector<NodePtr>& nodes) const {
  nodes.insert(nodes.end(), nodes_.begin(), nodes_.end());

  for (auto& [_, submodel] : submodels_) {
    submodel->CollectNodes(nodes);
  }
}

void RuntimeSubmodelVersion::SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler) {
  for (const auto& node : nodes_) {
    node->SetLoopScheduler(scheduler);
  }
  for (auto& [_, submodel] : submodels_) {
    submodel->SetLoopScheduler(scheduler);
  }
}

bool RuntimeSubmodelVersion::Empty() const {
  for (const auto& [_, output] : node_outputs_) {
    if (!output->Empty()) {
      return false;
    }
  }

  for (auto& [_, submodel] : submodels_) {
    if (!submodel->Empty()) {
      return false;
    }
  }

  return true;
}

void RuntimeSubmodelVersion::GatherRequiredVersions(
  std::vector<SubmodelVersionChain>& result, const std::unordered_set<std::string>& current_features
) {
  auto submodel_versions = SubmodelsByFeatures(current_features);

  for (const auto& [submodel_id, submodel] : submodels_) {
    std::string required_version;
    if (auto version_it = submodel_versions.find(submodel_id); version_it != submodel_versions.end()) {
      required_version = version_it->second;
    } else if (auto version_it = default_submodels_.find(submodel_id); version_it != default_submodels_.end()) {
      required_version = version_it->second.submodel_version;
    } else {
      continue;
    }

    SubmodelVersionChain chain = version_chain_;
    chain.ids_chain.push_back({.id = submodel_id, .version = required_version});
    result.push_back(chain);

    submodel->GatherRequiredVersions(result, required_version, current_features);
  }
}

std::unordered_map<std::string, std::string> RuntimeSubmodelVersion::SubmodelsByFeatures(
  const std::unordered_set<std::string>& current_features
) {
  std::unordered_map<std::string, std::string> result;
  for (const auto& features : features_) {
    bool is_active_set = true;
    for (const auto& feature : features.feature_ids) {
      is_active_set &= current_features.find(feature) != current_features.end();
    }

    if (is_active_set) {
      result.try_emplace(features.submodel_id, features.submodel_version);
    }
  }
  return result;
}

void RuntimeSubmodelVersion::CollectNodesTopsorted(std::vector<NodePtr>& nodes) const {
  for (const auto& item : Topsorted(nodes_, submodels_)) {
    std::visit(
      overloaded{
        [&nodes](const NodePtr& node) {
          nodes.push_back(node);
        },
        [&nodes](const RuntimeSubmodelPtr& submodel) {
          submodel->CollectNodesTopsorted(nodes);
        }
      },
      item
    );
  }
}

void RuntimeSubmodelVersion::WriteDot(
  std::ostream& subgraphs_out,
  std::ostream& edges_out,
  std::unordered_map<NodeOutputPtr, std::string>& node_output_names
) const {
  subgraphs_out << "subgraph" << " \"" << version_chain_.AsString() << "\" {" << std::endl;
  if (!version_chain_.ids_chain.empty()) {
    subgraphs_out << "label = \"" << version_chain_.ids_chain.back().version << "\";" << std::endl;
  }
  subgraphs_out << "color = blue;" << std::endl;
  subgraphs_out << "cluster = true;" << std::endl;

  for (const auto& [id, output] : node_outputs_) {
    std::string full_node_output_id = "\"" + (version_chain_.ids_chain.empty() ? "" : version_chain_.AsString() + "@")
      + id + "_output\"";
    if (node_output_names.emplace(output, full_node_output_id).second) {
      subgraphs_out << full_node_output_id << " [shape=box label=\"" << id << "\"]" << std::endl;
    }
  }

  for (const auto& [_, submodel] : submodels_) {
    submodel->WriteDot(subgraphs_out, edges_out, node_output_names);
  }

  for (const auto& node : nodes_) {
    std::string_view node_id = node->Id();
    if (auto sep = node_id.find('@'); sep != std::string_view::npos) {
      node_id = node_id.substr(sep + 1);
    }
    subgraphs_out << "\"" << node->Id() << "_node\"" << " [shape=ellipse label=\"" << node_id << "\"]" << std::endl;

    for (const auto& [output_id, output] : node->Outputs()) {
      edges_out << "\"" << node->Id() << "_node\"" << " -> " << node_output_names.at(output) << " [label=\""
                << output_id << "\"]" << std::endl;
    }

    for (const auto& [input_id, input] : node->Inputs()) {
      edges_out << node_output_names.at(input) << " -> \"" << node->Id() << "_node\"" << " [label=\"" << input_id
                << "\"]" << std::endl;
    }
  }

  subgraphs_out << "}" << std::endl;
}

} // namespace giga_edge
