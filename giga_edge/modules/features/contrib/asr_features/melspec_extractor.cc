#include "melspec_extractor.h"


namespace asr {

MelSpecExtractor::MelSpecExtractor(const FeatureParams& params) : params_(params) {
  if (params_.use_mfsc) {
    extractor_ = std::make_unique<Mfsc<float>>(params);
  } else {
    extractor_ = std::make_unique<PowerSpectrum<float>>(params);
  }
}

std::vector<float> MelSpecExtractor::GetFeatures(const std::vector<float>& wav) {
  std::vector<float> wav_copy(wav);
  std::vector<float> features;
  if (params_.normalize_gain) {
    asr::NormalizeGainInplace(&wav_copy);
  }
  features = extractor_->Apply(wav_copy);
  if (params_.normalize_features_across_axis_0) {
      asr::NormalizeAcrossAxis0Inplace(&features,
      {static_cast<int>(features.size() / static_cast<int>(params_.num_filterbank_chans)),
      static_cast<int>(params_.num_filterbank_chans)});
  }
  return features;
}

} // namespace spotter
