#include "giga_edge/lib/test_utils/matchers.h"

#include <catch.hpp>

#include <cmath>

namespace giga_edge {

void IsVectorsClose(const std::vector<float>& v1, const std::vector<float>& v2, float eps) {
  CAPTURE(v1.size());
  CAPTURE(v2.size());
  REQUIRE(v1.size() == v2.size());
  for (size_t i = 0; i < v2.size(); ++i) {
    CAPTURE(i);
    REQUIRE(v1[i] == Approx(v2[i]).margin(eps));
  }
}

} // namespace giga_edge