#include "giga_edge/modules/features/nodes/feature_iterator_with_reset/node.h"

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/modules/features/feature_extractor.h"

#include <iostream>

namespace giga_edge {

FeatureIteratorWithResetNode::FeatureIteratorWithResetNode(
  const NodeBaseConfig& base_config, const FeatureIteratorWithResetParams& params
)
  : NodeBase(base_config)
  , feature_iterator_(
      std::make_unique<ChunkIterator<float>>(ChunkIteratorParams{
        .chunk_size = params.context_width * params.num_mels, .step_size = params.step_size * params.num_mels
      })
    )
  , flush_tail_(params.flush_tail)
  , flush_tail_count_(params.flush_tail_count) {}

void FeatureIteratorWithResetNode::PushFeaturesWithReset(
  std::vector<float>&& features, bool need_reset, bool is_pushed_tail
) {
  const bool has_features = !features.empty();
  PushFeaturesBatch(std::move(features));
  PushNeedResetBatch(need_reset);
  if (has_features) {
    reset_pending_ = false;
  }
  PushPushFeaturesBatchFlag(is_pushed_tail);
}

void FeatureIteratorWithResetNode::ProcessOnce() {
  const auto& need_reset = GetNeedReset();
  if (need_reset.has_value() && need_reset.value().get()) {
    Reset();
  }
  feature_iterator_->Push(GetFeatures());

  auto step_features = feature_iterator_->Next();
  do {
    PushFeaturesWithReset(std::move(step_features), reset_pending_, false);
    step_features = feature_iterator_->Next();
  } while (!step_features.empty());

  const auto& push_features = GetPushFeatures();
  if (push_features.has_value() && push_features.value().get()) {
    auto all_features = feature_iterator_->GetAll();
    PushFeaturesWithReset(std::move(all_features), reset_pending_, true);
    for (int32_t i = 0; flush_tail_ && i < flush_tail_count_; ++i) {
      PushFeaturesWithReset({}, reset_pending_, true);
    }
  }
}

void FeatureIteratorWithResetNode::Reset() {
  feature_iterator_->Reset();
  reset_pending_ = true;
}

} // namespace giga_edge
