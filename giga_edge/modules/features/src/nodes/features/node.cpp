#include "giga_edge/modules/features/nodes/features/node.h"

#include "giga_edge/modules/features/feature_extractor.h"

namespace giga_edge {

FeaturesNode::FeaturesNode(const NodeBaseConfig& base_config, const FeatureParams& params)
  : NodeBase(base_config)
  , feature_extractor_(std::make_unique<StreamingFeatureExtractor>(params)) {}

void FeaturesNode::ProcessOnce() {
  if (const auto need_reset = GetNeedReset(); need_reset.has_value() && need_reset.value().get()) {
    feature_extractor_->Reset();
  }
  const auto& audio = GetAudio();
  PushFeatures(feature_extractor_->ExtractFeatures(audio));
}

void FeaturesNode::Reset() {
  feature_extractor_->Reset();
}

} // namespace giga_edge
