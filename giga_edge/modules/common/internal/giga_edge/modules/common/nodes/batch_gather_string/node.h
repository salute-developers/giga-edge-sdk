#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class BatchGatherStringNode : public NodeBase {
public:
  explicit BatchGatherStringNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

  REGISTER_BATCH_INPUT(batch_gather_string, input, Input, std::string, "Batch of string vectors to gather");
  REGISTER_OUTPUT(batch_gather_string, output, Output, std::string, "Gathered string vector");
};

REGISTER_NODE(batch_gather_string, BatchGatherStringNode, "Gathers a batch of string data into one vector");

} // namespace giga_edge
