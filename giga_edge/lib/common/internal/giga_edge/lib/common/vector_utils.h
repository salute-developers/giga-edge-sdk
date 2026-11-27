#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace giga_edge {

// L2 (Euclidean) metric distance ||a - b||_2. `a` and `b` must have the same size.
inline float L2Distance(const std::vector<float>& a, const std::vector<float>& b) {
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sum_sq += d * d;
  }
  return static_cast<float>(std::sqrt(sum_sq));
}

// Scales `v` to unit L2 length. Returns false if `v` is empty or has zero norm (leaves `v` unchanged).
inline bool L2NormalizeInPlace(std::vector<float>& v) {
  double sum_sq = 0.0;
  for (const float x : v) {
    const double xd = static_cast<double>(x);
    sum_sq += xd * xd;
  }
  if (!(sum_sq > 0.0)) {
    return false;
  }
  const float inv = static_cast<float>(1.0 / std::sqrt(sum_sq));
  for (float& x : v) {
    x *= inv;
  }
  return true;
}

} // namespace giga_edge
