#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/padding/params.h"

namespace giga_edge {

class PaddingNode : public NodeBase {
public:
  PaddingNode(const NodeBaseConfig& base_config, const PaddingParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(padding, input, Input, std::vector<float>, "Data");
  REGISTER_OUTPUT(padding, output, Output, std::vector<float>, "Padded data");

private:
  PaddingParams params_;
};

REGISTER_NODE(
  padding,
  PaddingNode,
  PaddingParams,
  "Input padding. Applies padding to every nonempty input according padding strategy from params."
);

} // namespace giga_edge