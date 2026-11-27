// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "tri_filterbank.h"

#include "speech_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace asr {

template<typename T>
std::vector<T> Linspace(T start, T end, int num) {
  std::vector<T> linspaced;

  if (num == 0) {
     return linspaced;
  }
  if (num == 1) {
      linspaced.push_back(start);
      return linspaced;
  }

  T delta = (end - start) / (num - 1);

  for (int i = 0; i < num - 1; ++i) {
      linspaced.push_back(start + delta * i);
  }
  linspaced.push_back(end);
  return linspaced;
}

template <typename T>
TriFilterbank<T>::TriFilterbank(
    int64_t numfilters,
    int64_t filterlen,
    int64_t samplingfreq,
    int64_t lowfreq /* = 0 */,
    int64_t highfreq /* = -1 */,
    FrequencyScale freqscale /* = FrequencyScale::MEL */)
    : num_filters_(numfilters),
      filter_len_(filterlen),
      sampling_freq_(samplingfreq),
      low_freq_(lowfreq),
      high_freq_((highfreq > 0) ? highfreq : (samplingfreq >> 1)),
      freq_scale_(freqscale),
      H_(filterlen * numfilters) {
  std::vector<T> frequencies = Linspace<T>(low_freq_, high_freq_, filterlen);

  T minwarpfreq = HertzToWarpedScale(low_freq_, freq_scale_);
  T maxwarpfreq = HertzToWarpedScale(high_freq_, freq_scale_);

  std::vector<T> mels = Linspace<T>(minwarpfreq, maxwarpfreq, num_filters_ + 2);
  for (int64_t i = 0; i < (num_filters_ + 2); ++i) {
    mels[i] = WarpedToHertzScale(mels[i], freq_scale_);
  }
  T min_h = 0.0;

  for (int64_t i = 0; i < filter_len_; ++i) {
    for (int64_t j = 0; j < num_filters_; ++j) {
      T loslope = (frequencies[i] - mels[j]) / (mels[j + 1] - mels[j]);
      T hislope = (mels[j + 2] - frequencies[i]) / (mels[j + 2] - mels[j + 1]);
      H_[i * num_filters_ + j] = std::max(std::min(hislope, loslope), min_h);
    }
  }
}

template <typename T>
std::vector<T> TriFilterbank<T>::Apply(const std::vector<T>& input) const {
  std::vector<T> output = CblasGemm(input, H_, num_filters_, filter_len_);
  std::transform(
      output.begin(), output.end(), output.begin(), [](T n) -> T {
        return n;
      });
  return output;
}

template <typename T>
std::vector<T> TriFilterbank<T>::Filterbank() const {
  return H_;
}


// Slaney mel scale: Slaney and SlaneyOut are ported from librosa (hz_to_mel, mel_to_hz), ISC license, see NOTICE.
double Slaney(double freq) {
  double f_min = 0.0;
  double f_sp = 200.0 / 3;

  double mels = (freq - f_min) / f_sp;

  double min_log_hz = 1000.0;  // beginning of log region (Hz)
  double min_log_mel = (min_log_hz - f_min) / f_sp;  // same (Mels)
  double logstep = std::log(6.4) / 27.0;  // step size for log region
  if (freq >= min_log_hz) {
    return min_log_mel + std::log(freq / min_log_hz) / logstep;
  }
  return mels;
}


double SlaneyOut(double mel) {
  double f_min = 0.0;
  double f_sp = 200.0 / 3;
  double freqs = f_min + f_sp * mel;
  double min_log_hz = 1000.0;
  double min_log_mel = (min_log_hz - f_min) / f_sp;
  double logstep = std::log(6.4) / 27.0;
  if (mel >= min_log_mel) {
    return min_log_hz * std::exp(logstep * (mel - min_log_mel));
  }
  return freqs;
}


template <typename T>
T TriFilterbank<T>::HertzToWarpedScale(T hz, FrequencyScale freqscale) const {
  switch (freqscale) {
    case FrequencyScale::MEL:
      return static_cast<T>(Slaney(static_cast<double>(hz)));
    case FrequencyScale::MELHTK:
      return 2595.0 * log10(1.0 + hz / 700.0);
    case FrequencyScale::LOG10:
      return log10(hz);
    case FrequencyScale::LINEAR:
      return hz;
    default:
      throw std::invalid_argument("TriFilterbank: unsupported frequency scale");
  }
}
template <typename T>
T TriFilterbank<T>::WarpedToHertzScale(T wrp, FrequencyScale freqscale) const {
  switch (freqscale) {
    case FrequencyScale::MEL:
      return static_cast<T>(SlaneyOut(static_cast<double>(wrp)));
    case FrequencyScale::MELHTK:
      return 700.0 * (pow(10, wrp / 2595.0) - 1);
    case FrequencyScale::LOG10:
      return pow(10, wrp);
    case FrequencyScale::LINEAR:
      return wrp;
    default:
      throw std::invalid_argument("TriFilterbank: unsupported frequency scale");
  }
}

template class TriFilterbank<float>;
template class TriFilterbank<double>;
}  // namespace asr
