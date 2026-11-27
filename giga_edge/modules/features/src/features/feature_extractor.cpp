#include "giga_edge/modules/features/feature_extractor.h"

#include "asr/melspec_extractor.h"
#include "giga_edge/lib/common/overloaded.h"
#include "spotter/melspec_extractor.h"

namespace giga_edge {

StreamingFeatureExtractor::StreamingFeatureExtractor(const FeatureParams& params)
  : params_(params) {
  std::visit(
    overloaded{
      [this](const asr::FeatureParams& asr_params) {
        feature_extractor_engine_ = std::make_unique<AsrMelSpecExtractor>(asr_params);
      },
      [this](const SpotterFeatureParams& spotter_params) {
        feature_extractor_engine_ = std::make_unique<SpotterMelSpecExtractor>(spotter_params);
      }
    },
    params_.params
  );
}

size_t StreamingFeatureExtractor::GetOffset(const std::vector<float>& features) const {
  return (features.size() / params_.num_mels) * params_.frame_stride_samples;
}

FeatureParams StreamingFeatureExtractor::GetParams() const {
  return params_;
}

std::vector<float> StreamingFeatureExtractor::ExtractFeaturesInternal(const std::vector<float>& wav) {
  return feature_extractor_engine_->GetFeatures(wav);
}

std::vector<float> StreamingFeatureExtractor::ExtractFeatures(const std::vector<float>& wav) {
  if (wav.empty()) {
    return {};
  }

  auto created_wav = CreateWav(wav);
  auto features = ExtractFeaturesInternal(created_wav);
  UpdateWavState(created_wav, GetOffset(features));
  return features;
}

void StreamingFeatureExtractor::Reset() {
  wav_state_.clear();
  feature_extractor_engine_->Reset();
}

void StreamingFeatureExtractor::UpdateWavState(const std::vector<float>& wav, size_t samples_count) {
  // Save right end wav offset for next features calculation.
  // ..............................
  //                      |________
  //                         offset
  if (samples_count >= wav.size()) {
    wav_state_ = {};
  } else {
    wav_state_ = std::vector<float>(wav.begin() + samples_count, wav.end());
  }
}

std::vector<float> StreamingFeatureExtractor::CreateWav(const std::vector<float>& wav) {
  std::vector<float> created_wav(wav_state_.begin(), wav_state_.end());
  created_wav.insert(created_wav.end(), wav.begin(), wav.end());

  return created_wav;
}

RawFeatureIterator::RawFeatureIterator(const FeatureIteratorParams& params)
  : params_(params)
  , need_erase_(false) {}

void RawFeatureIterator::Push(const std::vector<float>& new_features) {
  state_.insert(state_.end(), new_features.begin(), new_features.end());
}

std::pair<float*, size_t> RawFeatureIterator::Next() {
  if (need_erase_) {
    state_.erase(state_.begin(), state_.begin() + params_.step_size * params_.num_mels);
    need_erase_ = false;
  }

  if (state_.size() < params_.context_width * params_.num_mels) {
    return std::make_pair<float*, size_t>(nullptr, 0);
  }

  need_erase_ = true;

  return std::make_pair(state_.data(), params_.context_width * params_.num_mels);
}

void RawFeatureIterator::SetState(std::vector<float> state) {
  state_ = {state.begin(), state.end()};
  need_erase_ = false;
}

void RawFeatureIterator::Reset() {
  state_.clear();
  need_erase_ = false;
}

} // namespace giga_edge
