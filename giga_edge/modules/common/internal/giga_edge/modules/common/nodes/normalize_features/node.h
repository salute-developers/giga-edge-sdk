#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class NormalizeFeaturesNode : public NodeBase {
public:
  explicit NormalizeFeaturesNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(normalize_features, features, Features, std::vector<float>, "Input feature vector");
  REGISTER_OUTPUT(
    normalize_features,
    normalized_features,
    NormalizedFeatures,
    std::vector<float>,
    "L2-normalized copy of features (empty if input empty or zero norm)"
  );
};

REGISTER_NODE(normalize_features, NormalizeFeaturesNode, "L2-normalizes a float feature vector to unit length");

} // namespace giga_edge
