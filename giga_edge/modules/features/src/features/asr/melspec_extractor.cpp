#include "melspec_extractor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace giga_edge {

AsrMelSpecExtractor::AsrMelSpecExtractor(const asr::FeatureParams& params)
  : extractor_(params) {}

std::vector<float> AsrMelSpecExtractor::GetFeatures(const std::vector<float>& wav) {
  return extractor_.GetFeatures(wav);
}

} // namespace giga_edge
