#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"
#include "giga_edge/modules/assistant_core/text.h"
#include "giga_edge/modules/assistant_core/tool_call_parser.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("text helpers trim and measure marker overlaps", "[assistant][utils]") {
  REQUIRE(Trim("  \tтекст \n") == "текст");
  REQUIRE(Trim(" \n ").empty());
  REQUIRE(TrimTrailingNewlines("a\nb\r\n\n") == "a\nb");
  REQUIRE(TrimTrailingNewlines("\n").empty());
  REQUIRE(SuffixPrefixOverlap("abc<|mess", "<|message_sep|>") == 6);
  REQUIRE(SuffixPrefixOverlap("abc<|message_sep|>", "<|message_sep|>") == 15);
  REQUIRE(SuffixPrefixOverlap("abc", "<|message_sep|>") == 0);
  REQUIRE(SuffixPrefixOverlap("", "x") == 0);
  REQUIRE(PartialMarkerSuffix("text <tool_ca", "<tool_call>") == 8);
  REQUIRE(PartialMarkerSuffix("text <tool_call>", "<tool_call>") == 0);
  REQUIRE(PartialMarkerSuffix("text", "") == 0);
}

TEST_CASE("IsDecimalNumber takes the plain decimal notation only", "[assistant][utils]") {
  for (const auto* number : {"0", "7", "-2", "+15", "3.25", "-0.5", "+1.0", "007", "99999999999999999999"}) {
    CAPTURE(number);
    REQUIRE(IsDecimalNumber(number));
  }
  for (const auto* text : {
         "", "+", "-", ".5", "1.", "-.5", "1.2.3", "1,5", "+-1", "1e3", "0x10", "nan", "inf", " 1", "1 ", "1a", "1.5a"
       }) {
    CAPTURE(text);
    REQUIRE_FALSE(IsDecimalNumber(text));
  }
}

TEST_CASE("CutUtf8 keeps whole characters only", "[assistant][utils]") {
  REQUIRE(CutUtf8("абв", 100) == "абв");
  REQUIRE(CutUtf8("абв", 3) == "а");
  REQUIRE(CutUtf8("абв", 4) == "аб");
  REQUIRE(CutUtf8("ab", 1) == "a");
  REQUIRE(CutUtf8("", 5).empty());
  REQUIRE(CutUtf8(std::string("свет") + "\xD0", 100) == "свет\xEF\xBF\xBD");
  REQUIRE(CutUtf8(std::string("ok") + "\xE2\x82", 100) == "ok\xEF\xBF\xBD");
  REQUIRE(CutUtf8("а\xFFб", 100) == "а\xEF\xBF\xBDб");
  REQUIRE(CutUtf8("\xC0\xAF", 100) == "\xEF\xBF\xBD");
  REQUIRE(CutUtf8("\xED\xA0\x80", 100) == "\xEF\xBF\xBD");
  REQUIRE(CutUtf8("\xF4\x90\x80\x80", 100) == "\xEF\xBF\xBD");
  REQUIRE(CutUtf8("\xFF", 2).empty());
  REQUIRE(IsValidUtf8("свет €"));
  for (const std::string valid :
       {"\xC2\x80",
        "\xDF\xBF",
        "\xE0\xA0\x80",
        "\xED\x9F\xBF",
        "\xEE\x80\x80",
        "\xEF\xBF\xBF",
        "\xF0\x90\x80\x80",
        "\xF4\x8F\xBF\xBF"}) {
    INFO(valid.size());
    REQUIRE(IsValidUtf8(valid));
    REQUIRE(CutUtf8(valid, 100) == valid);
  }
  for (const std::string invalid :
       {"\xE0\x80\xAF", "\xF0\x80\x80\xAF", "\xC1\xBF", "\xF5\x80\x80\x80", "\xF8\x80", "\xE2\x82"}) {
    INFO(invalid.size());
    REQUIRE_FALSE(IsValidUtf8(invalid));
    REQUIRE(CutUtf8(invalid, 100) == "\xEF\xBF\xBD");
  }
  REQUIRE(CutUtf8("\x80\x80\x80", 100) == "\xEF\xBF\xBD");
  REQUIRE(CutUtf8("\xE2\x82" "A", 100) == "\xEF\xBF\xBD" "A");
  REQUIRE(CutUtf8("\xE2" "€", 100) == "\xEF\xBF\xBD" "€");
  REQUIRE(CutUtf8("a\xFF", 3) == "a");
  REQUIRE_FALSE(IsValidUtf8("а\xFFб"));
  REQUIRE_FALSE(IsValidUtf8("\xED\xA0\x80"));
  REQUIRE(CutUtf8("€", 3) == "€");
  REQUIRE(CutUtf8("€", 2).empty());
}

TEST_CASE("Utf8SequenceLength measures a sequence by its lead byte", "[assistant][utils]") {
  REQUIRE(Utf8SequenceLength('a') == 1);
  REQUIRE(Utf8SequenceLength(0xD0) == 2);
  REQUIRE(Utf8SequenceLength(0xE2) == 3);
  REQUIRE(Utf8SequenceLength(0xF0) == 4);
  REQUIRE(Utf8SequenceLength(0x80) == 0);
  REQUIRE(Utf8SequenceLength(0xF8) == 0);
  REQUIRE(IsValidUtf8(""));
  REQUIRE_FALSE(IsValidUtf8("\x80"));
}

TEST_CASE("json helpers parse strictly and write compact UTF-8", "[assistant][utils]") {
  Json::Value value;
  std::string error;
  REQUIRE(ParseJson(R"({"a": 1})", value, error));
  REQUIRE(value["a"] == 1);
  REQUIRE_FALSE(ParseJson(R"({"a": 1} tail)", value, error));
  REQUIRE_FALSE(ParseJson(R"({"a": 1, "a": 2})", value, error));
  REQUIRE_FALSE(ParseJson("", value, error));
  REQUIRE_FALSE(ParseJson(std::string(2000, '['), value, error));
  REQUIRE_THROWS(ParseJsonOrThrow("{"));
  REQUIRE(ParseJsonOrThrow("[1, 2]").size() == 2);
  Json::Value object;
  object["имя"] = "Лампа";
  object["n"] = 2.5;
  REQUIRE(WriteJson(object) == R"({"n":2.5,"имя":"Лампа"})");
  REQUIRE(JsonQuote("свет \"2\"\n\t") == R"("свет \"2\"\n\t")");
  REQUIRE(
    JsonQuote(
      std::string(
        "a\x01"
        "b"
      )
    )
    == R"("a\u0001b")"
  );
  // This is how jsoncpp 1.8 writes Cyrillic.
  REQUIRE(
    KeepJsonUtf8(R"({"a":"\u041c\u0430\u043a\u0441 1","b":"\\u0410\"\u0001","e":"\ud83c\udfb5"})")
    == R"({"a":"Макс 1","b":"\\u0410\"\u0001","e":"🎵"})"
  );
  REQUIRE(KeepJsonUtf8(R"(["\ud83c","\udfb5x","\u12"])") == R"(["\ud83c","\udfb5x","\u12"])");
  REQUIRE(KeepJsonUtf8("").empty());
  REQUIRE(JsonQuote("Макс Корж") == R"("Макс Корж")");
}
