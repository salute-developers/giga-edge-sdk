#pragma once

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/features/params.h"

namespace giga_edge {

class FeatureIteratorWithResetNode : public NodeBase {
public:
  FeatureIteratorWithResetNode(const NodeBaseConfig& base_config, const FeatureIteratorWithResetParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(feature_iterator_with_reset, features, Features, std::vector<float>, "Feature to iterate over");
  REGISTER_OPTIONAL_INPUT(feature_iterator_with_reset, push_features, PushFeatures, bool, "Push features");
  REGISTER_OPTIONAL_INPUT(
    feature_iterator_with_reset, need_reset, NeedReset, bool, "Reset flag aligned with emitted features batch"
  );
  REGISTER_BATCH_OUTPUT(
    feature_iterator_with_reset, features_batch, FeaturesBatch, std::vector<float>, "Batch of features"
  );
  REGISTER_BATCH_OUTPUT(
    feature_iterator_with_reset,
    need_reset_batch,
    NeedResetBatch,
    bool,
    "Reset flag aligned with emitted features batch"
  );
  REGISTER_OPTIONAL_BATCH_OUTPUT(
    feature_iterator_with_reset,
    push_features_batch,
    PushFeaturesBatchFlag,
    bool,
    "True for the batch emitted because of push_features (the tail of an utterance), false for regular batches. "
    "Travels in the same batch lane as features_batch, so it can drive downstream nodes that must not lag behind it "
    "(e.g. force_wait of an asynchronous model node)."
  );

private:
  void PushFeaturesWithReset(std::vector<float>&& features, bool need_reset, bool is_pushed_tail);

private:
  std::unique_ptr<ChunkIterator<float>> feature_iterator_;
  bool reset_pending_ = false;
  bool flush_tail_;
  int32_t flush_tail_count_;
};

REGISTER_NODE(
  feature_iterator_with_reset,
  FeatureIteratorWithResetNode,
  FeatureIteratorWithResetParams,
  "Iterate over features with sliding window and emit a reset flag aligned with the produced batch"
);

} // namespace giga_edge
