#include "giga_edge/modules/features/nodes/feature_iterator/node.h"

#include "giga_edge/lib/chunk_iterator/chunk_iterator.h"
#include "giga_edge/modules/features/feature_extractor.h"

namespace giga_edge {

FeatureIteratorNode::FeatureIteratorNode(const NodeBaseConfig& base_config, const FeatureIteratorParams& params)
  : NodeBase(base_config)
  , feature_iterator_(
      std::make_unique<ChunkIterator<float>>(ChunkIteratorParams{
        .chunk_size = params.context_width * params.num_mels, .step_size = params.step_size * params.num_mels
      })
    ) {}

void FeatureIteratorNode::ProcessOnce() {
  auto features = GetFeatures();
  feature_iterator_->Push(features);

  auto step_features = feature_iterator_->Next();
  do {
    PushFeaturesBatch(std::move(step_features));
    step_features = feature_iterator_->Next();
  } while (!step_features.empty());

  const auto& push_features = GetPushFeatures();
  if (push_features.has_value() && push_features.value().get()) {
    auto all_features = feature_iterator_->GetAll();
    if (!all_features.empty()) {
      PushFeaturesBatch(std::move(all_features));
    }
  }
}

void FeatureIteratorNode::Reset() {
  feature_iterator_->Reset();
}

} // namespace giga_edge
