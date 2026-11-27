#include "giga_edge/lib/math/math_expression.h"

#include <catch.hpp>

#include <cmath>

using namespace giga_edge;

TEST_CASE("Math expression, variable", "[math]") {
  auto expression = ParseMathExpression("var");
  REQUIRE(expression->Apply({{"var", 5.0f}}) == 5.0f);
  REQUIRE(expression->Apply({{"var", -1.5f}}) == -1.5f);
}

TEST_CASE("Math expression, number literal", "[math]") {
  REQUIRE(ParseMathExpression("32")->Apply({}) == 32.0f);
  REQUIRE(ParseMathExpression("1.5")->Apply({}) == 1.5f);
  REQUIRE(ParseMathExpression("  0.25  ")->Apply({}) == 0.25f);
}

TEST_CASE("Math expression, addition and subtraction", "[math]") {
  for (auto expression_str : {"a + b", "  a   +   b  ", "a+b"}) {
    INFO(expression_str);
    auto expression = ParseMathExpression(expression_str);
    REQUIRE(expression->Apply({{"a", 2.0f}, {"b", 3.0f}}) == 5.0f);
  }

  REQUIRE(ParseMathExpression("a - b")->Apply({{"a", 2.0f}, {"b", 3.0f}}) == -1.0f);
}

TEST_CASE("Math expression, multiplication and division", "[math]") {
  REQUIRE(ParseMathExpression("a * b")->Apply({{"a", 2.0f}, {"b", 3.0f}}) == 6.0f);
  REQUIRE(ParseMathExpression("a / b")->Apply({{"a", 3.0f}, {"b", 2.0f}}) == 1.5f);
}

TEST_CASE("Math expression, left associativity", "[math]") {
  REQUIRE(ParseMathExpression("10 - 3 - 2")->Apply({}) == 5.0f);
  REQUIRE(ParseMathExpression("8 / 4 / 2")->Apply({}) == 1.0f);
}

TEST_CASE("Math expression, operations priority", "[math]") {
  REQUIRE(ParseMathExpression("2 + 3 * 4")->Apply({}) == 14.0f);
  REQUIRE(ParseMathExpression("(2 + 3) * 4")->Apply({}) == 20.0f);
  REQUIRE(ParseMathExpression("2 - 6 / 3")->Apply({}) == 0.0f);
}

TEST_CASE("Math expression, unary minus", "[math]") {
  REQUIRE(ParseMathExpression("-var")->Apply({{"var", 5.0f}}) == -5.0f);
  REQUIRE(ParseMathExpression("-(a + b)")->Apply({{"a", 2.0f}, {"b", 3.0f}}) == -5.0f);
  REQUIRE(ParseMathExpression("a * -b")->Apply({{"a", 2.0f}, {"b", 3.0f}}) == -6.0f);
}

TEST_CASE("Math expression, complex", "[math]") {
  auto expression = ParseMathExpression("a + b / 32 - (x + 1) / y");
  REQUIRE(expression->Apply({{"a", 1.0f}, {"b", 64.0f}, {"x", 3.0f}, {"y", 2.0f}}) == 1.0f);
}

TEST_CASE("Math expression, division by zero gives infinity", "[math]") {
  REQUIRE(std::isinf(ParseMathExpression("a / b")->Apply({{"a", 1.0f}, {"b", 0.0f}})));
}

TEST_CASE("Math expression, invalid", "[math]") {
  REQUIRE_THROWS_AS(ParseMathExpression(""), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseMathExpression("a +"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseMathExpression("a + * b"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseMathExpression("(a + b"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseMathExpression("a b"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseMathExpression("1.5.2"), std::invalid_argument);
}

TEST_CASE("Math expression, get variables", "[math]") {
  auto expression = ParseMathExpression("a + b / 32 - (x + 1) / y");
  REQUIRE(expression->Variables() == std::unordered_set<std::string_view>{"a", "b", "x", "y"});

  REQUIRE(ParseMathExpression("2 * 2")->Variables().empty());
}

TEST_CASE("Math expression, ceil and floor", "[math]") {
  REQUIRE(ParseMathExpression("ceil(frames / 4)")->Apply({{"frames", 2501.0f}}) == 626.0f);
  REQUIRE(ParseMathExpression("ceil(frames / 4)")->Apply({{"frames", 2500.0f}}) == 625.0f);
  REQUIRE(ParseMathExpression("floor(x) + 1")->Apply({{"x", 2.7f}}) == 3.0f);
  REQUIRE(ParseMathExpression("-ceil(-1.5)")->Apply({}) == 1.0f);
  REQUIRE(ParseMathExpression(" ceil ( a * 2 ) ")->Apply({{"a", 0.3f}}) == 1.0f);
  REQUIRE(ParseMathExpression("ceil(frames / 4)")->Variables() == std::unordered_set<std::string_view>{"frames"});
  REQUIRE_THROWS(ParseMathExpression("sqrt(4)"));
  REQUIRE_THROWS(ParseMathExpression("ceil(4"));
}
