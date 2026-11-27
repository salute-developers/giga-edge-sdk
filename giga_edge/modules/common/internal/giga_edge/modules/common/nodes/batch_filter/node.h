#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class BatchFilterNode : public NodeBase {
public:
  BatchFilterNode(const NodeBaseConfig& base_config, BatchFilterNodeParams params);

  void ProcessOnce() override;

  REGISTER_BATCH_INPUT(batch_filter, input, Input, std::vector<float>, "Batch to filter");
  REGISTER_BATCH_OUTPUT(batch_filter, output, Output, std::vector<float>, "Filtered batch");

  void Reset() override {}

private:
  BatchFilterNodeParams params_;
};

REGISTER_NODE(batch_filter, BatchFilterNode, BatchFilterNodeParams, "Filters data inside batch");

} // namespace giga_edge