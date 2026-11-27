#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class BatchGatherNode : public NodeBase {
public:
  explicit BatchGatherNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_BATCH_INPUT(batch_gather, input, Input, std::vector<float>, "Batch to gather");
  REGISTER_OUTPUT(batch_gather, output, Output, std::vector<float>, "Filtered batch");
};

REGISTER_NODE(batch_gather, BatchGatherNode, "Gathers batch of data into one");

} // namespace giga_edge