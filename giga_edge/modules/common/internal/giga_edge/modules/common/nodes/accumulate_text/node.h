#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class AccumulateTextNode : public NodeBase {
public:
  AccumulateTextNode(const NodeBaseConfig& base_config, const AccumulateTextNodeParams& params);

  void ProcessOnce() override;
  void Reset() override;
  REGISTER_INPUT(accumulate_text, input, Input, std::string, "Text to accumulate.");
  REGISTER_INPUT(accumulate_text, push, Push, bool, "Signal to push the accumulated text and clear the buffer.");
  REGISTER_OPTIONAL_INPUT(accumulate_text, drop, Drop, bool, "Signal to clear the buffer without pushing.");
  REGISTER_OPTIONAL_INPUT(
    accumulate_text,
    restart,
    Restart,
    bool,
    "Signal to clear the buffer before this tick's input: the text starts anew from it."
  );

  REGISTER_OUTPUT(accumulate_text, output, Output, std::string, "The accumulated text.");

private:
  AccumulateTextNodeParams params_;
  std::string data_;
};

REGISTER_NODE(
  accumulate_text,
  AccumulateTextNode,
  AccumulateTextNodeParams,
  "Accumulates text until a push or drop signal is received."
);

} // namespace giga_edge
