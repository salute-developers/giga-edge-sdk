#include "giga_edge/lib/logical/logical_op.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Logical op, AND", "[logical]") {
  REQUIRE(LogicalOpBinary(false, false, LogicalOpType::AND) == false);
  REQUIRE(LogicalOpBinary(true, false, LogicalOpType::AND) == false);
  REQUIRE(LogicalOpBinary(false, true, LogicalOpType::AND) == false);
  REQUIRE(LogicalOpBinary(true, true, LogicalOpType::AND) == true);
}

TEST_CASE("Logical op, OR", "[logical]") {
  REQUIRE(LogicalOpBinary(false, false, LogicalOpType::OR) == false);
  REQUIRE(LogicalOpBinary(true, false, LogicalOpType::OR) == true);
  REQUIRE(LogicalOpBinary(false, true, LogicalOpType::OR) == true);
  REQUIRE(LogicalOpBinary(true, true, LogicalOpType::OR) == true);
}

TEST_CASE("Logical op, NOT", "[logical]") {
  REQUIRE(LogicalOpUnary(false, LogicalOpType::NOT) == true);
  REQUIRE(LogicalOpUnary(true, LogicalOpType::NOT) == false);
}

TEST_CASE("Logical op, errors", "[logical]") {
  REQUIRE_THROWS_AS(LogicalOpUnary(true, LogicalOpType::AND), std::invalid_argument);
  REQUIRE_THROWS_AS(LogicalOpUnary(true, LogicalOpType::OR), std::invalid_argument);
  REQUIRE_THROWS_AS(LogicalOpBinary(true, true, LogicalOpType::NOT), std::invalid_argument);
}