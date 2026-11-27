#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <deque>

namespace giga_edge {

class BatchifyNode : public NodeBase {
public:
  explicit BatchifyNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_BATCH_INPUT(batchify, batch, Batch, std::vector<float>, "Batch to take size from");
  REGISTER_INPUT(batchify, value, Value, float, "Value to multiply");

  REGISTER_BATCH_OUTPUT(batchify, output, Output, std::vector<float>, "Value repeated according to batch size");
};

REGISTER_NODE(batchify, BatchifyNode, "Repeats value size(batch) times");

} // namespace giga_edge
