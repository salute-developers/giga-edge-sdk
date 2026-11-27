#pragma once

#include "kissfft/kiss_fft.h"
namespace asr {
namespace fft {
using buffer_type = kiss_fft_cpx;
}  // namespace fft
}  // namespace asr

#include <vector>
#include <mutex>  // NOLINT

namespace asr {

template <typename T>
class FftCalculator {
 public:
  explicit FftCalculator(size_t nfft);
  ~FftCalculator();
  void Calculate(const size_t& n_samples, const size_t& k, const size_t& f, const T* begin, std::vector<T>& dft);

 private:
 private:
  const size_t nfft_;
  std::vector<fft::buffer_type> in_fft_buf_, out_fft_buf_;

  void CalculateKiss(const size_t& n_samples, const size_t& k, const size_t& f, const T* begin, std::vector<T>& dft);
  void HanningWindow(const int window_length, float* buffer);
  std::vector<float> fft_window_;
  kiss_fft_cfg cfg_;
};

}  // namespace asr
