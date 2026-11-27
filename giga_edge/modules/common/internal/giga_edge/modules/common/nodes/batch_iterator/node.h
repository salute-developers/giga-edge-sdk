#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {
class BatchIteratorNode : public NodeBase {
public:
  BatchIteratorNode(const NodeBaseConfig& base_config, const BatchIteratorNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(batch_iterator, data, Data, std::vector<float>, "Data");
  REGISTER_BATCH_OUTPUT(batch_iterator, batch, Batch, std::vector<float>, "Data splitted into batches of equal size");

private:
  BatchIteratorNodeParams params_;
  std::vector<float> data_;
};

REGISTER_NODE(batch_iterator, BatchIteratorNode, BatchIteratorNodeParams, "Split data into batches");

} // namespace giga_edge