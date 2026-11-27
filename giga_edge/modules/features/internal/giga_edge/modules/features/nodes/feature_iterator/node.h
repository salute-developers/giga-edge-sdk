#pragma once

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/features/params.h"

namespace giga_edge {

class FeatureIteratorNode : public NodeBase {
public:
  FeatureIteratorNode(const NodeBaseConfig& base_config, const FeatureIteratorParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(feature_iterator, features, Features, std::vector<float>, "Feature to iterate over");
  REGISTER_OPTIONAL_INPUT(feature_iterator, push_features, PushFeatures, bool, "Push features");
  REGISTER_BATCH_OUTPUT(feature_iterator, features_batch, FeaturesBatch, std::vector<float>, "Batch of features");

private:
  std::unique_ptr<ChunkIterator<float>> feature_iterator_;
};

REGISTER_NODE(
  feature_iterator, FeatureIteratorNode, FeatureIteratorParams, "Iterate over features with sliding window"
);

} // namespace giga_edge