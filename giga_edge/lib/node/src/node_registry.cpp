#include "giga_edge/lib/node/node_registry.h"

#include "giga_edge/lib/common/str_utils.h"

#include <iostream>

namespace giga_edge {

namespace {

void ValidateConnections(
  const std::string& node_id,
  const std::unordered_map<std::string, NodeOutputPtr>& connections,
  const std::unordered_map<std::string, NodeRegistry::NodeOutputInfo>& required_connections,
  const std::string& connection_type
) {
  std::vector<std::string> missing_connections;
  std::vector<std::string> extra_connections;

  for (const auto& [connection_name, connection_info] : required_connections) {
    if (connections.find(connection_name) == connections.end() && !connection_info.is_optional) {
      missing_connections.push_back(connection_name);
    }
  }

  for (const auto& [connection_name, _] : connections) {
    if (required_connections.find(connection_name) == required_connections.end()) {
      extra_connections.push_back(connection_name);
    }
  }

  if (!missing_connections.empty() || !extra_connections.empty()) {
    std::stringstream error;
    error << "Invalid " << connection_type << " for node '" << node_id << "':";

    if (!missing_connections.empty()) {
      error << " Missing: " << JoinStrings(missing_connections, ", ");
    }
    if (!extra_connections.empty()) {
      if (!missing_connections.empty()) {
        error << ";";
      }
      error << " Extra: " << JoinStrings(extra_connections, ", ");
    }

    throw std::runtime_error(error.str());
  }
}

} // namespace

bool NodeRegistry::RegisterNodeCreator(const std::string& id, NodeCreatorFunction f) {
  auto& map = GetNodeInfoMap();
  map[id].node_creator_function = f;
  return true;
}

bool NodeRegistry::RegisterNodeInput(
  const std::string& node_id,
  const std::string& input_id,
  const std::string& type,
  const std::string& description,
  bool is_batch,
  bool is_optional
) {
  auto& map = GetNodeInfoMap();
  return map[node_id]
    .inputs
    .try_emplace(
      input_id,
      NodeOutputInfo{
        .type = type,
        .description = description,
        .is_batch = is_batch,
        .is_optional = is_optional,
      }
    )
    .second;
}

bool NodeRegistry::RegisterNodeOutput(
  const std::string& node_id,
  const std::string& input_id,
  const std::string& type,
  const std::string& description,
  bool is_batch,
  bool is_recursive,
  bool is_optional
) {
  auto& map = GetNodeInfoMap();
  return map[node_id]
    .outputs
    .try_emplace(
      input_id,
      NodeOutputInfo{
        .type = type,
        .description = description,
        .is_batch = is_batch,
        .is_recursive = is_recursive,
        .is_optional = is_optional
      }
    )
    .second;
}

bool NodeRegistry::RegisterInfo(
  const std::string& node_id, const std::string& description, const std::string& filepath, int32_t line
) {
  auto& map = GetNodeInfoMap();
  auto root_filepath_pos_1 = filepath.find("giga_edge/lib/runtime");
  auto root_filepath_pos_2 = filepath.find("giga_edge/modules");
  auto root_filepath_pos = root_filepath_pos_1 != std::string::npos ? root_filepath_pos_1
    : root_filepath_pos_2 != std::string::npos
    ? root_filepath_pos_2
    : 0;
  map[node_id].description = description;
  map[node_id].filepath = filepath.substr(root_filepath_pos) + "#" + std::to_string(line);
  return true;
}

bool NodeRegistry::DeprecateNode(const std::string& node_id, const std::string& deprecation_reason) {
  auto& map = GetNodeInfoMap();
  map[node_id].deprecated = true;
  map[node_id].deprecation_reason = deprecation_reason;
  return true;
}

bool NodeRegistry::RegisterDynamicInputs(const std::string& node_id) {
  auto& map = GetNodeInfoMap();
  map[node_id].has_dynamic_inputs = true;
  return true;
}

bool NodeRegistry::RegisterDynamicOutputs(const std::string& node_id) {
  auto& map = GetNodeInfoMap();
  map[node_id].has_dynamic_outputs = true;
  return true;
}

std::shared_ptr<NodeBase> NodeRegistry::CreateNode(
  const std::string& id,
  const NodeBaseConfig& base_config,
  const IDataReaderPtr& data_reader,
  const YAML::Node& node_params
) {
  const auto& map = GetNodeInfoMap();
  auto it = map.find(id);
  if (it == map.end()) {
    throw std::runtime_error("Unknown node type " + id);
  }

  const auto& node_info = it->second;
  if (node_info.deprecated) {
    std::cerr << "Warning: node " << base_config.base_id << " has deprecated type " << id << ". "
              << node_info.deprecation_reason << std::endl;
  }

  auto node = node_info.node_creator_function(base_config, data_reader, node_params);

  if (node_info.has_dynamic_inputs) {
    auto required_inputs = node_info.inputs;
    for (const auto& input : node->GetDynamicInputs()) {
      required_inputs[input] = {};
    }
    ValidateConnections(base_config.base_id, base_config.inputs, required_inputs, "inputs");
  } else {
    ValidateConnections(base_config.base_id, base_config.inputs, node_info.inputs, "inputs");
  }

  if (node_info.has_dynamic_outputs) {
    auto required_outputs = node_info.outputs;
    for (const auto& output : node->GetDynamicOutputs()) {
      required_outputs[output] = {};
    }
    ValidateConnections(base_config.base_id, base_config.outputs, required_outputs, "outputs");
  } else {
    ValidateConnections(base_config.base_id, base_config.outputs, node_info.outputs, "outputs");
  }

  std::unordered_set<std::string_view> batch_inputs;
  for (const auto& [connection_name, connection_info] : it->second.inputs) {
    if (connection_info.is_batch) {
      batch_inputs.insert(connection_name);
    }
  }

  std::unordered_set<std::string_view> batch_outputs;
  std::unordered_set<std::string_view> recursive_outputs;
  for (const auto& [connection_name, connection_info] : it->second.outputs) {
    if (connection_info.is_batch) {
      batch_outputs.insert(connection_name);
    }
    if (connection_info.is_recursive) {
      recursive_outputs.insert(connection_name);
    }
  }

  node->SetConnectionsInfo(batch_inputs, batch_outputs, recursive_outputs);
  return node;
}

NodeRegistry::NodeInfoMap& NodeRegistry::GetNodeInfoMap() {
  static NodeRegistry::NodeInfoMap map;
  return map;
}

} // namespace giga_edge
