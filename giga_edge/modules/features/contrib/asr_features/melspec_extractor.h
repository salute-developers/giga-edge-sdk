#pragma once

#include "feature_params.h"
#include "mfsc.h"
#include "power_spectrum.h"

#include <memory>

namespace asr {

class MelSpecExtractor {
public:
  explicit MelSpecExtractor(const FeatureParams& params);

  std::vector<float> GetFeatures(const std::vector<float>& wav);

private:
  std::unique_ptr<PowerSpectrum<float>> extractor_;
  FeatureParams params_;
};

} // namespace asr
