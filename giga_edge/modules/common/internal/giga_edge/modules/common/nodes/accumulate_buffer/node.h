#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <deque>

namespace giga_edge {

class AccumulateBufferNode : public NodeBase {
public:
  AccumulateBufferNode(const NodeBaseConfig& base_config, const AccumulateBufferNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(accumulate_buffer, input, Input, std::vector<float>, "Data to accumulate");
  REGISTER_OPTIONAL_INPUT(accumulate_buffer, push, Push, bool, "Whether to push accumulated data");
  REGISTER_OPTIONAL_INPUT(accumulate_buffer, drop, Drop, bool, "Whether to drop accumulated data");

  REGISTER_OUTPUT(accumulate_buffer, output, Output, std::vector<float>, "Accumulated data");

private:
  AccumulateBufferNodeParams params_;
  std::deque<float> data_;
};

REGISTER_NODE(
  accumulate_buffer,
  AccumulateBufferNode,
  AccumulateBufferNodeParams,
  "Accumulates data until push or drop is fired (or max size reached). Drop takes higher priority"
);

} // namespace giga_edge
