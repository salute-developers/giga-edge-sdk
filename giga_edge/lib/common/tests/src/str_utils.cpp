#include "giga_edge/lib/common/str_utils.h"

#include <catch.hpp>

#include <set>
#include <vector>

using giga_edge::IsAlpha;
using giga_edge::JoinStrings;
using giga_edge::NormalizeTextToTokens;
using giga_edge::ReplaceAllInPlace;
using giga_edge::Split;
using giga_edge::SplitString;
using giga_edge::SplitStringTo;
using giga_edge::ToLowerLatinCyrillic;
using giga_edge::ToUpperLatinCyrillic;
using giga_edge::ToUtf16;
using giga_edge::ToUtf8;

TEST_CASE("Test JoinStrings", "[str_utils]") {
  REQUIRE(JoinStrings(std::vector<int>{1, 2, 3}, ", ") == "1, 2, 3");
  REQUIRE(JoinStrings(std::set<int>{1, 2, 3}, ", ") == "1, 2, 3");
  REQUIRE(JoinStrings(std::vector<std::string>{"a", "b", "c"}, ":") == "a:b:c");

  REQUIRE(
    JoinStrings(
      std::vector<int>{1, 2, 3},
      ", ",
      [](int x) {
        return x + 1;
      }
    )
    == "2, 3, 4"
  );
}

TEST_CASE("Test SplitString", "[str_utils]") {
  std::string s = "a:b:c";
  REQUIRE(SplitString(s, ':') == std::vector<std::string_view>{"a", "b", "c"});
}

TEST_CASE("Test SplitStringTo", "[str_utils]") {
  std::string a, b, c;
  std::string s = "a:b:c";
  SplitStringTo(s, ':', a, b, c);
  REQUIRE(a == "a");
  REQUIRE(b == "b");
  REQUIRE(c == "c");
}

TEST_CASE("Test ReplaceAllInPlace", "[str_utils]") {
  std::string s = "a{x}b{x}c";
  ReplaceAllInPlace(s, "{x}", "y");
  REQUIRE(s == "aybyc");
}

TEST_CASE("Test ReplaceAllInPlace empty pattern", "[str_utils]") {
  std::string s = "abc";
  ReplaceAllInPlace(s, "", "x");
  REQUIRE(s == "abc");
}

TEST_CASE("Test latin and cyrillic alpha helpers", "[str_utils]") {
  CHECK(IsAlpha(u'a'));
  CHECK(IsAlpha(u'Z'));
  CHECK(IsAlpha(u'я'));
  CHECK(IsAlpha(u'Ё'));
  CHECK_FALSE(IsAlpha(u' '));
  CHECK_FALSE(IsAlpha(u'-'));

  CHECK(ToUtf8(ToLowerLatinCyrillic(ToUtf16("AБЁ-"))) == "aбё-");
  CHECK(ToUpperLatinCyrillic(u'a') == u'A');
  CHECK(ToUpperLatinCyrillic(u'б') == u'Б');
  CHECK(ToUpperLatinCyrillic(u'я') == u'Я');
  CHECK(ToUpperLatinCyrillic(u'ё') == u'Ё');
  CHECK(ToUpperLatinCyrillic(u'Б') == u'Б');
  CHECK(ToUpperLatinCyrillic(u'-') == u'-');
  for (const char16_t ch : ToUtf16("abcxyzабвэюяё")) {
    CHECK(ToLowerLatinCyrillic(ToUpperLatinCyrillic(ch)) == ch);
  }
}

TEST_CASE("Utf8 and utf16 conversion keeps characters beyond the BMP", "[str_utils]") {
  const std::string text = "привет 😀 мир";

  CHECK(ToUtf8(ToUtf16(text)) == text);
  CHECK(ToUtf8(ToUtf16("😀")) == "😀");
}

TEST_CASE("Test text normalization to tokens", "[str_utils]") {
  const auto normalized = NormalizeTextToTokens("  А, Б Ё!  ", [](char16_t ch) {
    return ToLowerLatinCyrillic(ch);
  });
  const std::vector<std::string> expected_tokens{"а", "б", "ё"};

  CHECK(normalized.text == "а б ё");
  CHECK(normalized.tokens == expected_tokens);
}

TEST_CASE("Test SplitStringTo, bad arguments", "[str_utils]") {
  SECTION("Too many args") {
    std::string a, b, c;
    std::string s = "a:b";
    REQUIRE_THROWS_AS(SplitStringTo(s, ':', a, b, c), std::invalid_argument);
  }
  SECTION("Too few args") {
    std::string a, b, c;
    std::string s = "a:b:c:d";
    REQUIRE_THROWS_AS(SplitStringTo(s, ':', a, b, c), std::invalid_argument);
  }
}

TEST_CASE("Split parses whitespace separated floats and stops at the first non-number", "[str_utils]") {
  REQUIRE(Split("1 2.5 -3e2") == std::vector<float>{1.0f, 2.5f, -300.0f});
  REQUIRE(Split("  4\t5\n") == std::vector<float>{4.0f, 5.0f});
  REQUIRE(Split("").empty());
  REQUIRE(Split("x 1").empty());
  REQUIRE(Split("1 x 2") == std::vector<float>{1.0f});
}
