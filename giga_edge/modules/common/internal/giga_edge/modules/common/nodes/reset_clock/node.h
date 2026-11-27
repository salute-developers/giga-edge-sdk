#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class ResetClockNode : public NodeBase {
public:
  explicit ResetClockNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_DYNAMIC_INPUTS(reset_clock);
  REGISTER_OUTPUT(reset_clock, since_reset, SinceReset, int, "Chunks since last reset");

private:
  int since_last_reset_ = 0;
};

REGISTER_NODE(reset_clock, ResetClockNode, "Measures time from last model reset");

} // namespace giga_edge
