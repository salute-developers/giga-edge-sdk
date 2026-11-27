#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/node/event.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"

#include <string_view>

namespace giga_edge {

struct NodeContext {
  NodePtr node;
  std::unordered_map<std::string, NodeOutputPtr> inputs;
  std::unordered_map<std::string, NodeOutputPtr> outputs;
  NodeOutputPtr events;
  std::unordered_map<std::string, uint32_t> seq_ids;
  std::unique_ptr<SequenceIdGenerator> id_generator;
};

NodeContext CreateNode(
  const std::string& id,
  const std::string& params,
  std::unordered_map<std::string, IDataSpanPtr> data = {},
  std::unordered_set<std::string> optional_inputs = {},
  std::unordered_set<std::string> dynamic_inputs = {},
  std::unordered_set<std::string> dynamic_outputs = {},
  const EnvironmentsMap* environments = nullptr,
  std::unordered_set<std::string> optional_outputs = {}
);

NodeContext CreateNode(
  const std::string& id,
  const YAML::Node& params,
  std::unordered_map<std::string, IDataSpanPtr> data = {},
  std::unordered_set<std::string> optional_inputs = {},
  std::unordered_set<std::string> dynamic_inputs = {},
  std::unordered_set<std::string> dynamic_outputs = {},
  const EnvironmentsMap* environments = nullptr,
  std::unordered_set<std::string> optional_outputs = {}
);

NodeContext CreateNodeWithDataReader(
  const std::string& id,
  const std::string& params,
  const IDataReaderPtr& data_reader,
  std::unordered_set<std::string> optional_inputs = {},
  std::unordered_set<std::string> dynamic_inputs = {},
  std::unordered_set<std::string> dynamic_outputs = {},
  const EnvironmentsMap* environments = nullptr,
  std::unordered_set<std::string> optional_outputs = {}
);

NodeContext CreateNodeWithDataReader(
  const std::string& id,
  const YAML::Node& params,
  const IDataReaderPtr& data_reader,
  std::unordered_set<std::string> optional_inputs = {},
  std::unordered_set<std::string> dynamic_inputs = {},
  std::unordered_set<std::string> dynamic_outputs = {},
  const EnvironmentsMap* environments = nullptr,
  std::unordered_set<std::string> optional_outputs = {}
);

void NodeProcess(NodeContext& node_ctx, int32_t max_iterations = std::numeric_limits<int32_t>::max());

template<class InputType>
void NodePush(
  NodeContext& node_ctx,
  const std::string& input,
  const InputType& data,
  bool is_last = true,
  int32_t max_iterations = std::numeric_limits<int32_t>::max()
) {
  if (node_ctx.seq_ids.find(input) == node_ctx.seq_ids.end()) {
    node_ctx.seq_ids[input] = node_ctx.id_generator->GenerateNextInputId(input);
  }

  node_ctx.inputs.at(input)->Push(
    data,
    {
      .batch_infos = {{.id = node_ctx.seq_ids[input], .is_last = is_last}},
      .seq_id = node_ctx.seq_ids[input],
    }
  );

  NodeProcess(node_ctx, max_iterations);

  if (is_last) {
    node_ctx.seq_ids.erase(input);
  }
}

template<class InputType>
void NodePushSingle(
  NodeContext& node_ctx,
  const std::string& input,
  const InputType& data,
  int32_t max_iterations = std::numeric_limits<int32_t>::max()
) {
  if (node_ctx.seq_ids.find(input) == node_ctx.seq_ids.end()) {
    node_ctx.seq_ids[input] = node_ctx.id_generator->GenerateNextInputId(input);
  }

  node_ctx.inputs.at(input)->Push(data, {.seq_id = node_ctx.seq_ids[input]});

  NodeProcess(node_ctx, max_iterations);

  node_ctx.seq_ids.erase(input);
}

template<class OutputType>
OutputType NodeGet(NodeContext& node_ctx, const std::string& output) {
  auto result = node_ctx.outputs.at(output)->ConsumeSingle("test");
  return std::dynamic_pointer_cast<OutputValue<OutputType>>(result)->value();
}

template<class OutputType>
std::vector<OutputType> NodeGetBatch(NodeContext& node_ctx, const std::string& output) {
  auto batch = node_ctx.outputs.at(output)->ConsumeBatch("test");
  std::vector<OutputType> result;
  result.reserve(batch.size());
  for (const auto& item : batch) {
    result.push_back(std::dynamic_pointer_cast<OutputValue<OutputType>>(item)->value());
  }
  return result;
}

void NodePushEvent(NodeContext& node_ctx, Event event, int32_t max_iterations = std::numeric_limits<int32_t>::max());

IDataSpanPtr ToDataSpan(std::string_view s);

} // namespace giga_edge
