#include "giga_edge/modules/model/nodes/model/rnnt_model.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("RNNT rejects empty and nonpositive call limiter windows", "[runtime][nodes][onnx]") {
  REQUIRE_THROWS(CallLimiter(0, 1));
  REQUIRE_THROWS(CallLimiter(-1, 1));
  REQUIRE_THROWS(CallLimiter(1, 0));
}
