#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/features/feature_extractor.h"
#include "giga_edge/modules/features/params.h"

namespace giga_edge {

class FeaturesNode : public NodeBase {
public:
  FeaturesNode(const NodeBaseConfig& base_config, const FeatureParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(features, audio, Audio, std::vector<float>, "Audio to calculate features for");
  REGISTER_OPTIONAL_INPUT(
    features,
    need_reset,
    NeedReset,
    bool,
    "Drop the samples kept from the previous chunk before extracting: the next frame starts a new utterance"
  );
  REGISTER_OUTPUT(features, features, Features, std::vector<float>, "Calculated features");

private:
  std::unique_ptr<StreamingFeatureExtractor> feature_extractor_;
};

REGISTER_NODE(features, FeaturesNode, FeatureParams, "Calculate audio features");

} // namespace giga_edge