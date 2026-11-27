#include "fft_calculator.h"

#include <cmath>
#include <algorithm>

namespace  asr {

template <typename T>
FftCalculator<T>::FftCalculator(size_t nfft)
    : nfft_(nfft)
    , in_fft_buf_(nfft_)
    , out_fft_buf_(nfft_)
    , fft_window_(nfft_, 0.0)
{
  cfg_ = kiss_fft_alloc(nfft, 0, nullptr, nullptr);
  HanningWindow(nfft, this->fft_window_.data());
}

template <typename T>
void FftCalculator<T>::HanningWindow(const int window_length, float *buffer) {
  for (int i = 0; i < window_length; ++i) {
    buffer[i] = 0.5 * (1 - cos(2 * M_PI * i / (window_length - 1) * 1.0));
  }
}

template<typename T>
void FftCalculator<T>::Calculate(const size_t& n_samples, const size_t& k, const size_t& f, const T* begin,
                                 std::vector<T>& dft) {
  CalculateKiss(n_samples, k, f, begin, dft);
}

template<typename T>
void FftCalculator<T>::CalculateKiss(const size_t& n_samples, const size_t& k, const size_t& f, const T* begin,
                                     std::vector<T>& dft) {
  auto mx = std::max(n_samples, k);
  for (size_t i = 0; i < mx; ++i, ++begin) {
    if (i < n_samples) {
      // in_fft_buf_[i].r = *begin * fft_window_[i];
      in_fft_buf_[i].r = *begin;
      in_fft_buf_[i].i = 0.0f;
    }
    if (i < k) {
      out_fft_buf_[i].r = 0.0;
      out_fft_buf_[i].i = 0.0f;
    }
  }
  kiss_fft(cfg_, in_fft_buf_.data(), out_fft_buf_.data());
  for (size_t i = 0; i < k; ++i) {
    dft[f * k + i] = sqrt(out_fft_buf_[i].r * out_fft_buf_[i].r +
        out_fft_buf_[i].i * out_fft_buf_[i].i);
  }
}


template <typename T>
FftCalculator<T>::~FftCalculator() {
  kiss_fft_free(this->cfg_);
}

template class FftCalculator<float>;
template class FftCalculator<double>;
}  // namespace asr
