#pragma once

#include "matrix.h"

#include "giga_edge/modules/features/melspec_extractor.h"
#include "giga_edge/modules/features/spotter_features_params.h"

#include "kissfft/kiss_fft.h"

#include <cmath>
#include <string>
#include <vector>

namespace giga_edge {

constexpr int kRollingWindowSize = 30;

class SpotterMelSpecExtractor : public IMelSpecExtractor {
public:
  explicit SpotterMelSpecExtractor(const SpotterFeatureParams& params);

  std::vector<float> GetFeatures(const std::vector<float>& wav) override;

  void Reset() override;

  ~SpotterMelSpecExtractor() override;
private:
  SpotterFeatureParams params_;
  kiss_fft_cfg cfg_;

  Matrix rolling_means_;
  bool rolling_means_initialized_;
  kiss_fft_cpx* data_;
  kiss_fft_cpx* fft_result_;
  float* fft_window_;
  int nfft_;

  Matrix mel_basis_matrix_;

  void BuildMelBasis(int nfft);

  static void HammingWindow(int window_length, float* buffer);

  static void HanningWindow(int window_length, float* buffer);

  static float HzToMel(float freq);

  static float MelToHz(float mel);

  static std::vector<float> FftFrequencies(int nfreq, int sample_rate);

  static std::vector<float> MelFrequencies(int nmels, float fmin, float fmax);

  static std::vector<float> Diff(std::vector<float>* a);

  static std::vector<std::vector<float>> SubtractOuter(std::vector<float>* a, std::vector<float>* b);

  void NormalizeRolling(Matrix& result);

  template<typename T>
  static std::vector<float> GetLinspace(T start_in, T end_in, int num_in);
};

} // namespace giga_edge
