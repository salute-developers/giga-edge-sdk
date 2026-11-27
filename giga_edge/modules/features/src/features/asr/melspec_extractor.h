#pragma once

#include "giga_edge/modules/features/melspec_extractor.h"

#include "asr_features/melspec_extractor.h"

#include <cmath>
#include <string>
#include <vector>

namespace giga_edge {

class AsrMelSpecExtractor : public IMelSpecExtractor {
public:
  explicit AsrMelSpecExtractor(const asr::FeatureParams& params);

  std::vector<float> GetFeatures(const std::vector<float>& wav) override;

private:
  asr::MelSpecExtractor extractor_;
};

} // namespace giga_edge
