#include "giga_edge/modules/common/nodes/normalize_features/node.h"

#include "giga_edge/lib/common/vector_utils.h"

#include <vector>

namespace giga_edge {

NormalizeFeaturesNode::NormalizeFeaturesNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void NormalizeFeaturesNode::ProcessOnce() {
  const std::vector<float>& in = GetFeatures();
  if (in.empty()) {
    PushNormalizedFeatures({});
    return;
  }

  std::vector<float> out(in.begin(), in.end());
  if (!L2NormalizeInPlace(out)) {
    PushNormalizedFeatures({});
    return;
  }
  PushNormalizedFeatures(std::move(out));
}

} // namespace giga_edge
