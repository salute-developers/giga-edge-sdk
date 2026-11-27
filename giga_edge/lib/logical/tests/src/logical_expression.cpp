#include "giga_edge/lib/logical/logical_expression.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Logical expression, variable", "[logical]") {
  auto expression = ParseLogicalExpression("var");
  REQUIRE(expression->Apply({{"var", true}}) == true);
  REQUIRE(expression->Apply({{"var", false}}) == false);
}

TEST_CASE("Logical expression, not", "[logical]") {
  for (auto expression_str : {"!var", "    !  var  ", "not var"}) {
    INFO(expression_str);
    auto expression = ParseLogicalExpression(expression_str);

    REQUIRE(expression->Apply({{"var", true}}) == false);
    REQUIRE(expression->Apply({{"var", false}}) == true);
  }
}

TEST_CASE("Logical expression, and", "[logical]") {
  for (auto expression_str : {"var1 && var2", "  var1   &&   var2  ", "var1 & var2", "var1 and var2"}) {
    INFO(expression_str);
    auto expression = ParseLogicalExpression(expression_str);

    REQUIRE(expression->Apply({{"var1", true}, {"var2", true}}) == true);
    REQUIRE(expression->Apply({{"var1", true}, {"var2", false}}) == false);
    REQUIRE(expression->Apply({{"var1", false}, {"var2", true}}) == false);
    REQUIRE(expression->Apply({{"var1", false}, {"var2", false}}) == false);
  }
}

TEST_CASE("Logical expression, or", "[logical]") {
  for (auto expression_str : {"var1 || var2", "  var1   ||   var2  ", "var1 | var2", "var1 or var2"}) {
    INFO(expression_str);
    auto expression = ParseLogicalExpression(expression_str);

    REQUIRE(expression->Apply({{"var1", true}, {"var2", true}}) == true);
    REQUIRE(expression->Apply({{"var1", true}, {"var2", false}}) == true);
    REQUIRE(expression->Apply({{"var1", false}, {"var2", true}}) == true);
    REQUIRE(expression->Apply({{"var1", false}, {"var2", false}}) == false);
  }
}

TEST_CASE("Logical expression, complex", "[logical]") {
  auto expression = ParseLogicalExpression("!var1 || !!(var2 && (var3 || var1))");
  REQUIRE(expression->Apply({{"var1", true}, {"var2", true}, {"var3", false}}) == true);
}

TEST_CASE("Logical expression, operations priority", "[logical]") {
  auto expression = ParseLogicalExpression("var1 || var2 && !var3");
  REQUIRE(expression->Apply({{"var1", true}, {"var2", true}, {"var3", true}}) == true);

  expression = ParseLogicalExpression("(var1 || var2) && !var3");
  REQUIRE(expression->Apply({{"var1", true}, {"var2", true}, {"var3", true}}) == false);
}

TEST_CASE("Logical expression, invalid", "[logical]") {
  REQUIRE_THROWS_AS(ParseLogicalExpression("var1 and and var2"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseLogicalExpression("var1 ||| var2"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseLogicalExpression("var1 || && var2"), std::invalid_argument);
  REQUIRE_THROWS_AS(ParseLogicalExpression("var1 var2"), std::invalid_argument);
}

TEST_CASE("Logical expression, get variables", "[logical]") {
  auto expression = ParseLogicalExpression("!var1 || !!(var2 && (var3 || var1))");
  REQUIRE(expression->Variables() == std::unordered_set<std::string_view>{"var1", "var2", "var3"});
}