#include "giga_edge/lib/enum/enum.h"

#include <catch.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace giga_edge;

// The out-of-range static_casts below are the point of these tests: they assert that
// EnumToString rejects values outside the mapped range.
// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)

namespace {

template<typename T, typename F = std::less<void>>
auto RunTestCase(std::initializer_list<T> il, F less = F()) {
  auto input = std::vector(il);
  auto target(input);

  util::detail::SlowSort(input.data(), input.size(), less);
  std::sort(target.begin(), target.end(), less);

  REQUIRE(input == target);
}

struct ComplexValue {
  int i;
  std::string_view s;
  std::unique_ptr<int*> u; // move only

  bool operator==(const ComplexValue& o) const {
    return (i == o.i) && (s == o.s);
  }
};

std::ostream& operator<<(std::ostream& os, const ComplexValue& cv) {
  return os << '{' << cv.i << ", \"" << cv.s << "\"}";
}

} // namespace

TEST_CASE("test SlowSort for enum mapping", "[util][enum]") {
  SECTION("Ordered input") {
    RunTestCase({1, 2, 3, 4, 5, 6, 7, 8, 9});
  }

  SECTION("Reverse ordered input") {
    RunTestCase({20, 15, 9, 7, 6, 5, 3, 2, 1, 0});
  }

  SECTION("Unordered unique input") {
    RunTestCase({7, 6, 2, 9, 1, 10, 15, 20, 34, 32, 13, 17, 12});
  }

  SECTION("Unordered non-unique input") {
    RunTestCase({7, 6, 5, 5, 2, 9, 1, 10, 15, 5, 20, 34, 32, 13, 1, 20, 17, 12});
    RunTestCase({7, 6, 5, 5, 2, 9, 1, 10, 15, 5, 20, 34, 32, 13, 1, 20, 17, 12}, std::greater<void>());
  }

  SECTION("Corner cases") {
    RunTestCase({1});
    RunTestCase({1, 2});
    RunTestCase({2, 1});
    RunTestCase({1, 1});
  }

  SECTION("Complex value type") {
    std::array input{
      ComplexValue{1, "one"},
      ComplexValue{2, "two"},
      ComplexValue{3, "three"},
      ComplexValue{4, "four"},
      ComplexValue{5, "five"},
    };

    SECTION("standard string_view less function") {
      util::detail::SlowSort(input.data(), input.size(), [](const auto& a, const auto& b) {
        return std::less()(a.s, b.s);
      });

      // Lexicographical order.
      std::array target{
        ComplexValue{5, "five"},
        ComplexValue{4, "four"},
        ComplexValue{1, "one"},
        ComplexValue{3, "three"},
        ComplexValue{2, "two"},
      };
      REQUIRE(input == target);
    }

    SECTION("custom string_view less function") {
      util::detail::SlowSort(input.data(), input.size(), [](const auto& a, const auto& b) {
        return util::detail::SizeFirstIsStringLess(a.s, b.s);
      });

      // "Size-first" order.
      std::array target{
        ComplexValue{1, "one"},
        ComplexValue{2, "two"},
        ComplexValue{5, "five"},
        ComplexValue{4, "four"},
        ComplexValue{3, "three"},
      };
      REQUIRE(input == target);
    }
  }
}

enum class SimpleSequentialZeroBasedEnum {
  ZERO,
  ONE,
  TWO,
};

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(SimpleSequentialZeroBasedEnum, "zero", "one", "two-two");

TEST_CASE("test simple sequential zero-based enum mapping", "[util][enum]") {
  using Enum = SimpleSequentialZeroBasedEnum;

  REQUIRE(EnumToString(Enum::ZERO) == "zero");
  REQUIRE(EnumToString(Enum::ONE) == "one");
  REQUIRE(EnumToString(Enum::TWO) == "two-two");
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(3)), std::out_of_range);

  REQUIRE(EnumFromString<Enum>("zero") == Enum::ZERO);
  REQUIRE(EnumFromString<Enum>("one") == Enum::ONE);
  REQUIRE(EnumFromString<Enum>("two-two") == Enum::TWO);
  REQUIRE_THROWS_AS(EnumFromString<Enum>("two"), std::out_of_range);
}

enum class SequentiaZeroBasedlEnum { ZERO, ONE, TWO, VALUE_COUNT_ };

GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(
  SequentiaZeroBasedlEnum,
  {SequentiaZeroBasedlEnum::ZERO, {"zero", "null"}},
  {SequentiaZeroBasedlEnum::ONE, {"one"}},
  {SequentiaZeroBasedlEnum::TWO, {"two", "two-two", "2"}},
)

TEST_CASE("test complex sequential zero-based enum mapping", "[util][enum]") {
  using Enum = SequentiaZeroBasedlEnum;

  REQUIRE(EnumToString(Enum::ZERO) == "zero");
  REQUIRE(EnumToString(Enum::ONE) == "one");
  REQUIRE(EnumToString(Enum::TWO) == "two");
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(3)), std::out_of_range);

  REQUIRE(EnumFromString<Enum>("zero") == Enum::ZERO);
  REQUIRE(EnumFromString<Enum>("null") == Enum::ZERO);
  REQUIRE(EnumFromString<Enum>("one") == Enum::ONE);
  REQUIRE(EnumFromString<Enum>("two") == Enum::TWO);
  REQUIRE(EnumFromString<Enum>("two-two") == Enum::TWO);
  REQUIRE(EnumFromString<Enum>("2") == Enum::TWO);
  REQUIRE_THROWS_AS(EnumFromString<Enum>("1"), std::out_of_range);
}

enum class ComplexEnum : signed short { M_10 = -10, M_9, M_8, VALUE_COUNT_ = 3 };

GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(
  ComplexEnum,
  {ComplexEnum::M_10, {"-10", "minus ten"}},
  {ComplexEnum::M_9, {"-9"}},
  {ComplexEnum::M_8, {"minus eight", "-8", "минус восемь"}},
)

TEST_CASE("test complex generic sequential enum mapping", "[util][enum]") {
  using Enum = ComplexEnum;

  REQUIRE(EnumToString(Enum::M_10) == "-10");
  REQUIRE(EnumToString(Enum::M_9) == "-9");
  REQUIRE(EnumToString(Enum::M_8) == "minus eight");
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(-11)), std::out_of_range);
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(-7)), std::out_of_range);
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(0)), std::out_of_range);
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(10)), std::out_of_range);

  REQUIRE(EnumFromString<Enum>("-10") == Enum::M_10);
  REQUIRE(EnumFromString<Enum>("minus ten") == Enum::M_10);
  REQUIRE(EnumFromString<Enum>("-9") == Enum::M_9);
  REQUIRE(EnumFromString<Enum>("minus eight") == Enum::M_8);
  REQUIRE(EnumFromString<Enum>("-8") == Enum::M_8);
  REQUIRE(EnumFromString<Enum>("минус восемь") == Enum::M_8);
  REQUIRE_THROWS_AS(EnumFromString<Enum>("0"), std::out_of_range);
}

enum class GenericEnumManualValueCount {
  ZERO,
  ONE,
  TEN = 10,
  MINUS_FIVE = -5,

#if 1
  VALUE_COUNT_ = 4
#else
  VALUE_COUNT_ = 3 // should not compile
#endif
};

// Should compile.
GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(
  GenericEnumManualValueCount,
  {GenericEnumManualValueCount::ZERO, {"a"}},
  {GenericEnumManualValueCount::ONE, {"b"}},
  {GenericEnumManualValueCount::TEN, {"c"}},
  {GenericEnumManualValueCount::MINUS_FIVE, {"d"}},
);

enum class GenericEnum : signed char { ZERO, ONE, TEN = 10, MINUS_FIVE = -5, VALUE_COUNT_ };

GIGA_EDGE_DEFINE_COMPLEX_ENUM_MAPPING(
  GenericEnum,
  {GenericEnum::ZERO, {"zero", "null"}},
  {GenericEnum::ONE, {"one"}},
  {GenericEnum::TEN, {"ten", "10", "десять"}},
  {GenericEnum::MINUS_FIVE, {"-5"}},
  {GenericEnum::VALUE_COUNT_, {"-4", "VALUE_COUNT_"}},
)

TEST_CASE("test generic enum mapping", "[util][enum]") {
  using Enum = GenericEnum;

  REQUIRE(EnumToString(Enum::ZERO) == "zero");
  REQUIRE(EnumToString(Enum::ONE) == "one");
  REQUIRE(EnumToString(Enum::TEN) == "ten");
  REQUIRE(EnumToString(Enum::MINUS_FIVE) == "-5");
  REQUIRE(EnumToString(Enum::VALUE_COUNT_) == "-4");
  REQUIRE_THROWS_AS(EnumToString(static_cast<Enum>(3)), std::out_of_range);

  REQUIRE(EnumFromString<Enum>("zero") == Enum::ZERO);
  REQUIRE(EnumFromString<Enum>("null") == Enum::ZERO);
  REQUIRE(EnumFromString<Enum>("one") == Enum::ONE);
  REQUIRE(EnumFromString<Enum>("ten") == Enum::TEN);
  REQUIRE(EnumFromString<Enum>("10") == Enum::TEN);
  REQUIRE(EnumFromString<Enum>("десять") == Enum::TEN);
  REQUIRE(EnumFromString<Enum>("-5") == Enum::MINUS_FIVE);
  REQUIRE(EnumFromString<Enum>("-4") == Enum::VALUE_COUNT_);
  REQUIRE(EnumFromString<Enum>("VALUE_COUNT_") == Enum::VALUE_COUNT_);
  REQUIRE_THROWS_AS(EnumFromString<Enum>("1"), std::out_of_range);
}

// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
