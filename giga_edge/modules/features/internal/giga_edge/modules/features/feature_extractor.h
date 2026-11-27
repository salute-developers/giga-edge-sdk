#pragma once

#include "melspec_extractor.h"
#include "params.h"

#include <deque>
#include <memory>
#include <vector>

namespace giga_edge {

// StreamingFeatureExtractor is able to generate features
// from stream of speech samples.
// Features can demand right speech samples context
// and cannot be calculated until more samples are fed.
// StreamingFeatureExtractor saves samples in wav_state
// and loads them every time ExtractFeatures method is called.
class StreamingFeatureExtractor {
public:
  explicit StreamingFeatureExtractor(const FeatureParams& params);

  ~StreamingFeatureExtractor() = default;

  StreamingFeatureExtractor(StreamingFeatureExtractor&&) = default;

  std::vector<float> ExtractFeatures(const std::vector<float>& wav);

  void Reset();

  FeatureParams GetParams() const;

private:
  std::vector<float> ExtractFeaturesInternal(const std::vector<float>& wav);

  size_t GetOffset(const std::vector<float>& features) const;

  void UpdateWavState(const std::vector<float>& wav, size_t samples_count);

  std::vector<float> CreateWav(const std::vector<float>& wav);

private:
  FeatureParams params_;
  std::vector<float> wav_state_;
  std::unique_ptr<IMelSpecExtractor> feature_extractor_engine_;
};

class RawFeatureIterator {
public:
  explicit RawFeatureIterator(const FeatureIteratorParams& params);

  void Push(const std::vector<float>& new_features);

  // Result is valid only until next call.
  std::pair<float*, size_t> Next();

  void SetState(std::vector<float> state);

  void Reset();

private:
  FeatureIteratorParams params_;
  std::vector<float> state_;
  bool need_erase_;
};

} // namespace giga_edge
