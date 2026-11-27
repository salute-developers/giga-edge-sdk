#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/data_reader/helpers.h"

namespace giga_edge {

NodeContext CreateNode(
  const std::string& id,
  const std::string& params,
  std::unordered_map<std::string, IDataSpanPtr> data,
  std::unordered_set<std::string> optional_inputs,
  std::unordered_set<std::string> dynamic_inputs,
  std::unordered_set<std::string> dynamic_outputs,
  const EnvironmentsMap* environments,
  std::unordered_set<std::string> optional_outputs
) {
  return CreateNode(
    id,
    ParseYaml(params.data(), params.size()),
    data,
    optional_inputs,
    dynamic_inputs,
    dynamic_outputs,
    environments,
    std::move(optional_outputs)
  );
}

NodeContext CreateNode(
  const std::string& id,
  const YAML::Node& params,
  std::unordered_map<std::string, IDataSpanPtr> data,
  std::unordered_set<std::string> optional_inputs,
  std::unordered_set<std::string> dynamic_inputs,
  std::unordered_set<std::string> dynamic_outputs,
  const EnvironmentsMap* environments,
  std::unordered_set<std::string> optional_outputs
) {
  return CreateNodeWithDataReader(
    id,
    params,
    CreateCachingDataReader(nullptr, std::move(data)),
    std::move(optional_inputs),
    std::move(dynamic_inputs),
    std::move(dynamic_outputs),
    environments,
    std::move(optional_outputs)
  );
}

NodeContext CreateNodeWithDataReader(
  const std::string& id,
  const std::string& params,
  const IDataReaderPtr& data_reader,
  std::unordered_set<std::string> optional_inputs,
  std::unordered_set<std::string> dynamic_inputs,
  std::unordered_set<std::string> dynamic_outputs,
  const EnvironmentsMap* environments,
  std::unordered_set<std::string> optional_outputs
) {
  return CreateNodeWithDataReader(
    id,
    ParseYaml(params.data(), params.size()),
    data_reader,
    optional_inputs,
    dynamic_inputs,
    dynamic_outputs,
    environments,
    std::move(optional_outputs)
  );
}

NodeContext CreateNodeWithDataReader(
  const std::string& id,
  const YAML::Node& params,
  const IDataReaderPtr& data_reader,
  std::unordered_set<std::string> optional_inputs,
  std::unordered_set<std::string> dynamic_inputs,
  std::unordered_set<std::string> dynamic_outputs,
  const EnvironmentsMap* environments,
  std::unordered_set<std::string> optional_outputs
) {
  NodeContext result;
  for (const auto& [input, input_info] : NodeRegistry::GetNodeInfoMap()[id].inputs) {
    if (!input_info.is_optional || optional_inputs.find(input) != optional_inputs.end()) {
      result.inputs[input] = std::make_shared<NodeOutput>(input);
    }
  }

  for (const auto& input : dynamic_inputs) {
    result.inputs[input] = std::make_shared<NodeOutput>(input);
  }

  for (const auto& [output, output_info] : NodeRegistry::GetNodeInfoMap()[id].outputs) {
    if (output_info.is_optional && optional_outputs.find(output) == optional_outputs.end()) {
      continue;
    }
    result.outputs[output] = std::make_shared<NodeOutput>(output);
    result.outputs[output]->Subscribe("test", []() {});
  }

  for (const auto& output : dynamic_outputs) {
    result.outputs[output] = std::make_shared<NodeOutput>(output);
    result.outputs[output]->Subscribe("test", []() {});
  }

  result.events = std::make_shared<NodeOutput>("events");

  std::vector<std::string> input_names;
  input_names.reserve(result.inputs.size());
  for (const auto& [input_name, _] : result.inputs) {
    input_names.push_back(input_name);
  }
  result.id_generator = std::make_unique<SequenceIdGenerator>(input_names);

  NodeBaseConfig node_base_config{
    .submodel_version_chain = {},
    .events = result.events,
    .enqueue_callback = [](NodePtr) {},
    .common_node_params = {},
    .next_seq_id = result.id_generator->NextSeqId(),
    .environments = environments,
    .base_id = id,
    .inputs = result.inputs,
    .outputs = result.outputs,
    .priority = 0,
  };

  result.node = NodeRegistry::CreateNode(id, node_base_config, data_reader, params);
  result.node->Start();
  return result;
}

void NodePushEvent(NodeContext& node_ctx, Event event, int32_t max_iterations) {
  node_ctx.events->Push(event, {.seq_id = node_ctx.id_generator->GenerateNextEventId()});
  NodeProcess(node_ctx, max_iterations);
}

void NodeProcess(NodeContext& node_ctx, int32_t max_iterations) {
  for (int32_t i = 0; i < max_iterations && node_ctx.node->GetNextProcessingType() != NextProcessingType::NONE; ++i) {
    node_ctx.node->Process();
  }
}

IDataSpanPtr ToDataSpan(std::string_view s) {
  return Create<IDataSpanPtr>(std::vector<char>(s.data(), s.data() + s.size()));
}

} // namespace giga_edge
