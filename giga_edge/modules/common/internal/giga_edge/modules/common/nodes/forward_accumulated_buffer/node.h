#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ForwardAccumulatedBufferNode : public NodeBase {
public:
  explicit ForwardAccumulatedBufferNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(forward_accumulated_buffer, input, Input, std::string, "New data to accumulate");
  REGISTER_OPTIONAL_INPUT(forward_accumulated_buffer, drop, Drop, bool, "Whether to reset accumulated data");

  REGISTER_OUTPUT(forward_accumulated_buffer, output, Output, std::string, "Accumulated data");
  REGISTER_OUTPUT(forward_accumulated_buffer, is_buffer_changed, IsBufferChanged, bool, "Check is buffer changed");

private:
  std::string data_;
};

REGISTER_NODE(
  forward_accumulated_buffer,
  ForwardAccumulatedBufferNode,
  "A node for accumulating a string value and sending it to output"
);

} // namespace giga_edge
