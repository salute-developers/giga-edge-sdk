#include "giga_edge/lib/common/vector_utils.h"

#include <catch.hpp>

#include <cmath>

using giga_edge::L2Distance;
using giga_edge::L2NormalizeInPlace;

TEST_CASE("L2NormalizeInPlace scales 3,4 to unit", "[vector_utils]") {
  std::vector<float> v{3.f, 4.f};
  REQUIRE(L2NormalizeInPlace(v));
  REQUIRE(v[0] == Approx(0.6f).margin(1e-5f));
  REQUIRE(v[1] == Approx(0.8f).margin(1e-5f));
}

TEST_CASE("L2NormalizeInPlace leaves unit vector unchanged", "[vector_utils]") {
  std::vector<float> v{1.f, 0.f};
  REQUIRE(L2NormalizeInPlace(v));
  REQUIRE(v[0] == Approx(1.f).margin(1e-6f));
  REQUIRE(v[1] == Approx(0.f).margin(1e-6f));
}

TEST_CASE("L2NormalizeInPlace fails on zero norm", "[vector_utils]") {
  std::vector<float> v{0.f, 0.f};
  REQUIRE_FALSE(L2NormalizeInPlace(v));
  REQUIRE(v[0] == 0.f);
  REQUIRE(v[1] == 0.f);
}

TEST_CASE("L2Distance is zero for identical vectors", "[vector_utils]") {
  const std::vector<float> a{1.f, 2.f, 3.f};
  REQUIRE(L2Distance(a, a) == Approx(0.f).margin(1e-6f));
}

TEST_CASE("L2Distance orthogonal unit vectors in R2", "[vector_utils]") {
  const std::vector<float> x{1.f, 0.f};
  const std::vector<float> y{0.f, 1.f};
  REQUIRE(L2Distance(x, y) == Approx(std::sqrt(2.f)).margin(1e-5f));
}
