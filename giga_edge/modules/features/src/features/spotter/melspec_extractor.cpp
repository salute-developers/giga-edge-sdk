#include "melspec_extractor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace giga_edge {

SpotterMelSpecExtractor::SpotterMelSpecExtractor(const SpotterFeatureParams& params)
  : params_(params)
  , rolling_means_(1, kRollingWindowSize)
  , mel_basis_matrix_(0, 0) {
  rolling_means_initialized_ = false;

  nfft_ = (params.num_freq - 1) * 2;

  BuildMelBasis(nfft_);

  cfg_ = kiss_fft_alloc(nfft_, 0, nullptr, nullptr);
  data_ = new kiss_fft_cpx[nfft_];
  fft_result_ = new kiss_fft_cpx[nfft_];

  fft_window_ = new float[nfft_];
  HanningWindow(nfft_, this->fft_window_);
}

SpotterMelSpecExtractor::~SpotterMelSpecExtractor() {
  kiss_fft_free(this->cfg_);
  delete[] this->data_;
  delete[] this->fft_result_;
  delete[] this->fft_window_;
}

void SpotterMelSpecExtractor::HammingWindow(const int window_length, float* buffer) {
  for (int i = 0; i < window_length; ++i) {
    buffer[i] = 0.54 - (0.46 * cos(2 * M_PI * (i / ((window_length - 1) * 1.0))));
  }
}

void SpotterMelSpecExtractor::HanningWindow(const int window_length, float* buffer) {
  for (int i = 0; i < window_length; ++i) {
    buffer[i] = 0.5 * (1 - cos(2 * M_PI * i / (window_length - 1) * 1.0));
  }
}

template<typename T>
std::vector<float> SpotterMelSpecExtractor::GetLinspace(T start_in, T end_in, int num_in) {
  std::vector<float> linspaced;

  auto start = static_cast<double>(start_in);
  auto end = static_cast<double>(end_in);
  auto num = static_cast<double>(num_in);

  if (num == 0) {
    return linspaced;
  }

  if (num == 1) {
    linspaced.push_back(start);
    return linspaced;
  }

  double delta = (end - start) / (num - 1);

  for (int i = 0; i < num - 1; ++i) {
    linspaced.push_back(start + delta * i);
  }
  linspaced.push_back(end);

  return linspaced;
}

std::vector<float> SpotterMelSpecExtractor::FftFrequencies(int num_freq, int sample_rate) {
  return GetLinspace(0.0f, static_cast<float>(sample_rate) / 2.0f, num_freq);
}

float SpotterMelSpecExtractor::HzToMel(const float freq) {
  float fmin = 0.0f;
  float fsp = 200.0f / 3;
  float mels = (freq - fmin) / fsp;
  float min_log_hz = 1000.0f;
  float min_log_mel = (min_log_hz - fmin) / fsp;
  float log_step = log(6.4) / 27.0;

  if (freq >= min_log_hz) {
    mels = min_log_mel + log(freq / min_log_hz) / log_step;
  }

  return mels;
}

float SpotterMelSpecExtractor::MelToHz(const float mel) {
  float fmin = 0.0f;
  float fsp = 200.0f / 3;
  float freq = fmin + fsp * mel;
  float min_log_hz = 1000.0f;
  float min_log_mel = (min_log_hz - fmin) / fsp;
  float log_step = log(6.4) / 27.0;

  if (mel >= min_log_mel) {
    freq = min_log_hz * exp(log_step * (mel - min_log_mel));
  }

  return freq;
}

std::vector<float> SpotterMelSpecExtractor::MelFrequencies(const int nmels, const float fmin, const float fmax) {
  float min_mel = HzToMel(fmin);
  float max_mel = HzToMel(fmax);

  std::vector<float> mels_freq = GetLinspace(min_mel, max_mel, nmels);
  for (float& i : mels_freq) {
    i = MelToHz(i);
  }
  return mels_freq;
}

std::vector<float> SpotterMelSpecExtractor::Diff(std::vector<float>* a) {
  std::vector<float> out;
  out.reserve(a->size());
  for (size_t i = 0; i < (*a).size() - 1; ++i) {
    out.push_back((*a)[i + 1] - (*a)[i]);
  }
  return out;
}

std::vector<std::vector<float>> SpotterMelSpecExtractor::SubtractOuter(std::vector<float>* a, std::vector<float>* b) {
  std::vector<std::vector<float>> result;
  for (size_t i = 0; i < (*a).size(); ++i) {
    result.emplace_back();
    for (float j : (*b)) {
      result[i].push_back((*a)[i] - j);
    }
  }

  return result;
}

void SpotterMelSpecExtractor::BuildMelBasis(int nfft) {
  float fmin = 0.0f;
  float fmax = params_.sampling_freq / 2.0f;
  mel_basis_matrix_ = Matrix(1 + (nfft / 2), params_.num_mels);

  std::vector<float> fft_freqs = FftFrequencies(params_.num_freq, params_.sampling_freq);
  std::vector<float> mel_f = MelFrequencies(params_.num_mels + 2, fmin, fmax);
  std::vector<float> f_diff = Diff(&mel_f);
  std::vector<std::vector<float>> ramps = SubtractOuter(&mel_f, &fft_freqs);

  for (int i = 0; i < params_.num_mels; ++i) {
    float enorm = 2.0f / (mel_f[i + 2] - mel_f[i]);

    for (size_t j = 0; j < ramps[i].size(); j++) {
      float lower = -ramps[i][j] / f_diff[i];
      float upper = ramps[i + 2][j] / f_diff[i + 1];
      mel_basis_matrix_(j, i) = std::max(0.0f, std::min(lower, upper));
      mel_basis_matrix_(j, i) *= enorm;
    }
  }
}

std::vector<float> SpotterMelSpecExtractor::GetFeatures(const std::vector<float>& wav) {
  size_t chunk_position = 0;
  size_t frames_num;
  if (static_cast<int>(wav.size()) >= params_.frame_size_samples) {
    frames_num = (wav.size() - params_.frame_size_samples) / params_.frame_stride_samples + 1;
  } else {
    frames_num = 0;
  }
  size_t valuable_fft_size = 1 + nfft_ / 2;
  Matrix spectrum_matrix(frames_num, valuable_fft_size);

  const float* current_signal_ptr;
  for (size_t frame = 0; frame < frames_num; ++frame) {
    current_signal_ptr = wav.data() + chunk_position;

    for (int i = 0; i < nfft_; ++i, ++current_signal_ptr) {
      data_[i].r = *current_signal_ptr * fft_window_[i];
      data_[i].i = 0.0f;
    }

    kiss_fft(cfg_, data_, fft_result_);

    for (int i = 0; i < params_.num_freq; ++i) {
      spectrum_matrix(frame, i) = sqrt(fft_result_[i].r * fft_result_[i].r + fft_result_[i].i * fft_result_[i].i);
    }

    chunk_position += params_.frame_stride_samples;
  }

  // Two normalizations: to the reference level, or over a rolling window.
  float epsilon = 1e-5f;
  if (params_.rolling_norm) {
    epsilon = pow(10., 0.05 * params_.min_db);
  }

  auto result = (spectrum_matrix * mel_basis_matrix_);
  for (size_t i = 0; i < result.Rows(); ++i) {
    for (size_t j = 0; j < result.Cols(); ++j) {
      auto power = std::max(result(i, j), epsilon);
      result(i, j) = log10(power) * 20.0;
    }
  }

  if (params_.rolling_norm) {
    NormalizeRolling(result);
  } else {
    auto divider = -(params_.min_db - params_.ref_db);
    for (size_t row = 0; row < result.Rows(); row += 1) {
      for (size_t col = 0; col < result.Cols(); col += 1) {
        result(row, col) = (result(row, col) - params_.min_db) / divider;
      }
    }
  }

  return result.Flatten();
}

void SpotterMelSpecExtractor::NormalizeRolling(Matrix& result) {
  if (result.Rows() == 0 || result.Cols() == 0) {
    return;
  }

  auto spec_means = result.RowMean();
  if (!rolling_means_initialized_) {
    for (size_t col = 0; col < kRollingWindowSize; col += 1) {
      rolling_means_(0, col) = spec_means[0];
    }
    rolling_means_initialized_ = true;
  }

  for (size_t row = 0; row < result.Rows(); row += 1) {
    for (size_t col = 1; col < kRollingWindowSize; col += 1) {
      rolling_means_(0, col - 1) = rolling_means_(0, col);
    }
    rolling_means_(0, kRollingWindowSize - 1) = spec_means[row];

    float current_rolling_mean = rolling_means_.RowMean()[0];
    for (size_t col = 0; col < result.Cols(); col += 1) {
      result(row, col) -= current_rolling_mean;
    }
  }

  for (size_t row = 0; row < result.Rows(); row += 1) {
    for (size_t col = 0; col < result.Cols(); col += 1) {
      result(row, col) /= -params_.ref_db;
    }
  }
}

void SpotterMelSpecExtractor::Reset() {
  rolling_means_initialized_ = false;
}

} // namespace giga_edge
