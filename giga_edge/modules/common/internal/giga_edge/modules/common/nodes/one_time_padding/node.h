#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/padding/params.h"

namespace giga_edge {

class OneTimePaddingNode : public NodeBase {
public:
  OneTimePaddingNode(const NodeBaseConfig& base_config, const PaddingParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(one_time_padding, input, Input, std::vector<float>, "Data");
  REGISTER_OPTIONAL_INPUT(
    one_time_padding, need_reset, NeedReset, bool, "Start of a new stream: pad this tick's input again"
  );
  REGISTER_OUTPUT(one_time_padding, output, Output, std::vector<float>, "Padded data");

private:
  PaddingParams params_;
  bool activated_;
};

REGISTER_NODE(one_time_padding, OneTimePaddingNode, PaddingParams, "One time input padding");

} // namespace giga_edge